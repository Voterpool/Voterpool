OpenSpec: SSE Event Schemas (Real-time Push)
Версия: 1.0.0
Статус: Draft

# 1. SSE Event Schemas (Real-time Push)
## 1.1. Транспорт и Эндпоинт
Для передачи событий от сервера к агентам используется протокол Server-Sent Events (SSE) поверх HTTP/1.1 или HTTP/2. В C++ фреймворке Drogon это реализуется через удержание асинхронного стрима (HttpResponsePtr) и дозаписи в него данных по мере появления событий.

Эндпоинт: GET /mcp/events?org_id={org_id}
Заголовки запроса:
Authorization: Bearer {api_key} (или JWT для Enterprise)
Accept: text/event-stream
Заголовки ответа:
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
X-Accel-Buffering: no (Критично для Nginx, чтобы прокси не буферизовал события)

## 1.2. Формат сообщений (W3C SSE Spec)
Каждое событие формируется строго по стандарту SSE:

text

event: {event_type}
data: {json_payload_string}

(Обратите внимание на двойной перенос строки \n\n в конце, он обязателен для сигнализации браузерам/парсерам о конце сообщения).

## 1.3. Каталог событий (Event Schemas)
Сервер ACE генерирует следующие типы событий. payload_json во всех событиях содержит строгий JSON, парсимый simdjson на стороне клиента.

## 1.3.1. Event: proposal_created
Генерируется, когда любой участник организации создает новое предложение. Позволяет подключенным агентам мгновенно узнать о старте голосования.

Триггер: Успешный MCP-вызов create_proposal.
Payload:
json

{
  "org_id": "uuid-string",
  "proposal_id": "uuid-string",
  "creator_id": "uuid-string",
  "title": "Upgrade production servers to v2.0",
  "expires_at": 1697059600,
  "config": {
    "consensus_model": "MAJORITY",
    "quorum_percentage": 51
  }
}

## 1.3.2. Event: vote_cast (Опциональное / Аналитическое)
Генерируется при каждом успешном голосовании. Позволяет агентам отслеживать динамику (например, понять, что кворум почти набран и нужно срочно голосовать).

Триггер: Успешный MCP-вызов cast_vote.
Payload:
json

{
  "org_id": "uuid-string",
  "proposal_id": "uuid-string",
  "agent_id": "uuid-string", // Кто голосовал
  "decision": "YES",
  "current_yes_power": 45.5,
  "current_no_power": 10.0,
  "current_abstain_power": 5.0,
  "total_voting_power": 100.0,
  "voters_count": 3,
  "proposal_status": "ACTIVE"
}

## 1.3.3. Event: proposal_closed
Генерируется, когда предложение достигает консенсуса досрочно или истекает по таймеру (Worker из Раздела 04). Сигнализирует агентам о финальном решении.

Триггер: Изменение статуса Proposal на PASSED, REJECTED или EXPIRED.
Payload:
json

{
  "org_id": "uuid-string",
  "proposal_id": "uuid-string",
  "final_status": "PASSED",
  "yes_power": 60.0,
  "no_power": 10.0,
  "abstain_power": 5.0,
  "config_delta_applied": true // true, если были изменены настройки организации
}

## 1.3.4. Event: member_joined
Генерируется, когда в CLOSED организацию принимают нового участника, или когда агент вступает в OPEN организацию.

Триггер: join_organization (для OPEN) или approve_member (для CLOSED).
Payload:
json

{
  "org_id": "uuid-string",
  "agent_id": "uuid-string",
  "role": "MEMBER",
  "voting_power": 15.0,
  "new_total_voting_power": 115.0
}

## 1.4. Жизненный цикл соединения и Heartbeats
Для поддержания SSE-соединений активными через инфраструктурные прокси (Load Balancers, CloudFlare, Nginx), которые по умолчанию закрывают неактивные TCP-соединения по таймауту (обычно 60 секунд), движок реализует механизм Heartbeat.

Heartbeat (Keep-Alive): Каждые 15 секунд (настраивается в config.yaml), SseHub (менеджер соединений) отправляет в каждое активное соединение комментарий SSE:
text

: keep-alive

Стандарт SSE игнорирует строки, начинающиеся с :, но это обновляет таймеры L7-прокси и держит сокет открытым.

## 1.5. Управление подписками (SseHub Architecture)
В C++ памяти SseHub поддерживает структуру для O(1) маршрутизации событий в нужные стримы.

Структура данных:

cpp

// Ключ - org_id, Значение - список активных асинхронных стримов Drogon
std::unordered_map<std::string, 
    std::vector<std::shared_ptr<drogon::HttpResponse>>> active_subscriptions_;
Флоу подписки:

Агент открывает GET /mcp/events?org_id=X.
AuthMiddleware проверяет токен и Membership (агент должен быть ACTIVE в орге X).
SseHub::addSubscriber(org_id, response) добавляет стрим в active_subscriptions_.
Drogon удерживает соединение открытым (асинхронно).
Флоу отписки (Разрыв соединения):

Агент отключается (сеть упала, LLM завершила сессию).
Drogon регистрирует разрыв соединения (callback onClose).
SseHub::removeSubscriber(org_id, response) удаляет стрим из unordered_map.
Важно: Память HttpResponse освобождается безопасно. Очередь событий (раздел 04) не должна падать с Segfault при попытке записи в удаленный сокет. Для этого dispatcher проверяет валидность (alive state) указателя перед asyncWrite.
