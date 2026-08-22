OpenSpec: Фоновые процессы (Background Workers & Lifecycle)
Версия: 1.0.0
Статус: Draft

# 1. Фоновые процессы (Background Workers & Lifecycle)

## 1.1. Архитектура фоновых процессов

Для предотвращения блокировки асинхронного I/O Drogon, все тяжелые или периодические задачи выносятся в пул фоновых потоков. В C++20 для этого используется std::jthread в связке с std::stop_token (для грациозной остановки) и std::condition_variable (для пробуждения по таймеру).

Основные воркеры:

Proposal Expiration Worker (TTL Manager) — закрывает просроченные предложения.
SSE Event Dispatcher — асинхронно рассылает пуш-уведомления агентам.
RocksDB Maintenance Worker — управляет компакцией и сборкой мусора.

## 1.2. Proposal Expiration Worker (TTL Manager)

Отвечает за обработку предложений, у которых истекло время expires_at.

## 1.2.1. Механика работы (Оптимизация Prefix-Scan)

Для поиска просроченных предложений не используется сканирование всей базы. Задействуется вторичный индекс (CF cf_indexes), где ключи отсортированы по времени:

Формат ключа индекса: active_proposals:{expires_at}:{proposal_id}
Значение: org_id
Алгоритм воркера (цикл каждые 1 секунду):

Получить текущий timestamp now = std::chrono::system_clock::now().time_since_epoch().
Создать итератор RocksDB по CF cf_indexes.
Выполнить Seek("active_proposals:").
Пока ключ начинается с active_proposals::
Распарсить expires_at из ключа (строковые операции над фиксированным префиксом; simdjson здесь не требуется — это не JSON).
Если expires_at > now, прервать цикл (итератор отсортирован, дальше просроченных нет).
Если expires_at <= now:
Извлечь org_id и proposal_id.
Выполнить C++ корутину closeProposal(proposal_id, org_id).
Уснуть на 1 секунду (std::this_thread::sleep_for).

## 1.2.2. Корутина closeProposal

При закрытии просроченного предложения воркер выполняет ACID-транзакцию WriteBatch:

Читает объект Proposal из cf_proposals.
Если статус уже не ACTIVE (например, консенсус достигнут досрочно), удаляет ключ из индекса и выходит.
Вызывает IConsensusModel::evaluate() для финального расчета.
Обновляет статус Proposal на PASSED, REJECTED или EXPIRED.
В рамках одной транзакции WriteBatch: Первым делом удаляет ключ из cf_indexes (чтобы исключить повторную обработку при сбое), затем обновляет статус Proposal.
Если статус PASSED и есть config_delta, применяет новые настройки к Organization.
Помещает событие proposal_closed в очередь SSE Event Dispatcher'а.

Важно: closeProposal ОБЯЗАН захватывать тот же per-proposal мьютекс (см. docs/01 §5), что и ConsensusEngine::cast_vote, до чтения/модификации Proposal. Это исключает гонку между досрочным закрытием по таймеру и параллельным голосованием по одному и тому же предложению. Удаление ключа из cf_indexes и обновление статуса Proposal остаются в рамках одной транзакции WriteBatch под этим замком.

## 1.3. SSE Event Dispatcher

Если в cast_vote или в closeProposal консенсус достигнут, необходимо мгновенно оповестить всех подключенных агентов этой организации. Запись в сеть (HTTP/2 SSE) напрямую из потока RocksDB/Консенсуса заблокировала бы его.

## 1.3.1. Очередь событий (Lock-Free Queue)

В C++ памяти используется неблокирующая очередь (например, moodycamel::ConcurrentQueue или boost::lockfree::queue).

Producer: Потоки, обрабатывающие голоса или TTL, формируют структуру SseEvent и кладут её в очередь за O(1).
Структура SseEvent:
cpp

struct SseEvent {
std::string org_id;
std::string event_type; // "proposal_created", "proposal_closed", "vote_cast"
std::string payload_json;
};

## 1.3.2. Воркер-Диспатчер

Отдельный поток читает очередь пачками (batch) и асинхронно отправляет их в Drogon соединения.

Воркер читает до 1000 событий из очереди.
Для каждого события ищет активные SSE-стримы в SseHub (хэш-таблица std::unordered_map<org_id, std::vector<HttpResponsePtr>>).
Формирует SSE-сообщение: event: proposal_closed\ndata: {payload_json}\n\n.
Вызывает безопасную передачу данных в цикл Drogon через drogon::app().getLoop()->queueInLoop(...). Прямая запись в HttpResponsePtr из фонового потока запрещена из-за риска Data Race и Segfault.

## 1.4. RocksDB Maintenance Worker

[Enterprise — отложено до MVP] Так как система работает как автономная БД, воркер берет на себя инфраструктурные задачи:

Flush MemTable to SST: Контроль сброса оперативной памяти на диск (если стандартный триггер RocksDB не сработал из-за пиковой нагрузки).
Compaction: Запуск CompactRangeOptions в часы минимальной нагрузки (настраивается в config.yaml) для дефрагментации SST-файлов. Это предотвращает деградацию производительности чтения при длительной работе системы (месяцы аптайма).

## 1.5. Lifecycle & Graceful Shutdown (Управление жизненным циклом)

Enterprise-система должна корректно обрабатывать сигналы ОС (SIGTERM, SIGINT) для предотвращения потери данных при деплоях в Kubernetes.

## 1.5.1. Фаза Shutdown

При получении сигнала SIGTERM:

Stop Drogon: Движок перестает принимать новые HTTP/MCP соединения и закрывает слушающие сокеты.
Drain SSE Connections: Асинхронно закрывает все активные SSE-стримы с кодом события server_shutdown.
Stop Workers: Вызывает request_stop() для std::jthread воркеров.
TTL Worker завершает текущую итерацию сканирования и выходит.
SSE Dispatcher выгребает оставшиеся события из очереди в сеть (Drain), затем завершается.
Flush RocksDB: Вызывает rocksdb::DB::Flush() (синхронно дожидается сброса всех MemTable на диск) и rocksdb::DB::Close().
Stop spdlog: Сбрасывает буферы асинхронного логгера.
Процесс завершается с кодом 0.

## 1.5.2. Фаза Startup (Recovery)

При запуске (холодный старт после сбоя):

RocksDB Open: Открывает БД с проверкой WAL (Write-Ahead Log). Если предыдущий процесс упал, RocksDB автоматически реплеит незакоммиченные транзакции из WAL в SST.
Schema Gate: проверка meta:schema_version; при необходимости — цепочка миграций (docs/12) ДО старта воркеров.
Index Recovery (Опционально): Воркер делает быстрое сканирование cf_proposals для восстановления индексов active_proposals:, если они были повреждены.
Start Drogon & Workers: Поднимаются пулы потоков Drogon и запускаются std::jthread.
Health Check Ready: Сервер начинает отвечать 200 OK на GET /health.
