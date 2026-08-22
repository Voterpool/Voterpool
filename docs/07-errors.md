OpenSpec: Обработка ошибок (JSON-RPC Error Codes)
Версия: 1.0.0
Статус: Draft

# 1. Обработка ошибок 
## 1.1. Формат ответа при ошибке
При возникновении любой ошибки валидации или бизнес-логики сервер возвращает HTTP-статус 200 OK. В теле возвращается стандартный объект JSON-RPC 2.0 с полем error. Маппинг кастомных ошибок на HTTP статусы (4xx) не производится, чтобы LLM-агенты могли парсировать ошибки из единого контракта. Исключения ровно два: Parse error → HTTP 400; деградация БД (-32050) → HTTP 503 (см. §1.5).

Базовая структура:

json

{
  "jsonrpc": "2.0",
  "id": "request_id_or_null",
  "error": {
    "code": -32001,
    "message": "Unauthorized",
    "data": {
      "reason": "Invalid or expired API token",
      "details": {}
    }
  }
}
Примечание: Поле data является обязательным для кастомных ошибок (коды от -32000 до -32099) и опциональным для стандартных ошибок JSON-RPC.

## 1.2. Стандартные ошибки JSON-RPC (Транспортный уровень)
Обрабатываются на уровне парсинга simdjson и маршрутизации Drogon до выполнения бизнес-логики.

Код
Message
Описание (Триггер)
-32700	Parse error	Ошибка парсинга JSON (например, передан невалидный JSON-синтаксис). id запроса устанавливается в null.
-32600	Invalid Request	Запрос не является валидным объектом JSON-RPC 2.0 (отсутствует jsonrpc или method); нарушение протокола MCP 2026-07-28: неизвестная версия в MCP-Protocol-Version, несоответствие Mcp-Name = params.name (docs/05 §1.0).
-32601	Method not found	Передано неизвестное имя MCP Tool (отсутствует в списке register_agent, create_organization и т.д.).
-32602	Invalid params	Отсутствуют обязательные аргументы, неверный тип данных (например, строка вместо числа) или невалидный формат UUID.
-32603	Internal error	Непредвиденная ошибка C++ (например, исключение std::bad_alloc или сбой RocksDB). Логируется на сервере, агенту возвращается общий текст.

## 1.3. Кастомные ошибки приложения (Бизнес-логика)
Используют диапазон кодов от -32000 до -32099.

-32001: Unauthorized
Триггер: Отсутствует заголовок Authorization, токен не найден в БД (Native), или JWT невалиден (OIDC).
Пример data:
json

{
  "reason": "Auth token missing or invalid"
}
-32002: Forbidden
Триггер: Агент аутентифицирован, но не имеет прав на выполнение действия.
Примеры data:
Не состоит в орге: {"reason": "Agent is not a member of this organization", "org_id": "..."}
Статус PENDING: {"reason": "Membership is pending admin approval", "org_id": "..."}
Нет прав админа: {"reason": "Admin privileges required to execute this action"}
-32003: Conflict
Триггер: Нарушение бизнес-логики состояния (попытка изменить неизменяемое состояние).
Примеры data:
Двойное голосование: {"reason": "Agent has already voted on this proposal", "proposal_id": "...", "previous_decision": "YES"}
Голосование закрыто: {"reason": "Proposal is already closed", "current_status": "PASSED"}
Орга уже существует: {"reason": "Organization with this name already exists"}
-32004: Not Found
Триггер: Указанный org_id или proposal_id не существует в RocksDB.
Пример data:
json

{
  "reason": "Entity not found",
  "entity_type": "Proposal",
  "entity_id": "a1b2c3d4-..."
}
-32005: Business Rule Violation
Триггер: Запрос логически неверен в контексте текущих настроек организации.
Примеры data:
Неверная сила голоса: {"reason": "Sum of voting powers cannot exceed 100% in SHARES model", "attempted_sum": 105.0}
Неверный config_delta: {"reason": "Cannot change power_distribution model while active proposals exist"}
Истекшее время: {"reason": "Voting duration must be greater than 0 seconds"}
Недопустимый вариант голоса: {"reason": "Decision is not allowed by the consensus model", "consensus_model": "QUORUM_PERCENTAGE", "allowed": ["YES", "NO"]}
Лимит участников: {"reason": "Organization is full", "max_agents": 100}
Дневной лимит вступлений: {"reason": "Daily join limit reached", "joins_per_day_limit": 20}
Последний админ: {"reason": "Last admin cannot leave; transfer admin rights first"}
Два действия сразу: {"reason": "Only one of config_delta or action is allowed per proposal"}
-32006: Rate Limit Exceeded
Триггер: Агент превысил лимит запросов (RPS) для предотвращения спама/DoS (Enterprise защита).
Пример data:
json

{
  "reason": "Rate limit exceeded",
  "retry_after_sec": 10
}
-32050: Server Overloaded / DB Unhealthy
Триггер: деградация хранилища — db_healthy_ == false после IOError/Corruption RocksDB (см. §1.5).
Пример data:
json

{
  "reason": "Storage backend unavailable",
  "retry_after_sec": 30
}

## 1.5. Деградация БД (Disk Full / IOError) и Backpressure

Проблема: при заполнении диска на 100% rocksdb::DB::Write() вернёт Status::IOError. Необработанная ошибка уронит соединение по таймауту, и агенты будут бесконечно повторять cast_vote — само-DoS по уже больному серверу.

Реакция движка (обязательное поведение):

Если RocksDB Write()/Open()/Flush() возвращает Status с IsIOError() (переполнен диск, отказ прав, аппаратный сбой) или IsCorruption() — движок устанавливает атомарный флаг db_healthy_ = false (единый на процесс) и пишет spdlog::critical с текстом статуса.
Pre-handling Advice проверяет флаг ПЕРВЫМ делом, до разбора тела и любых обращений к БД: при db_healthy_ == false ВСЕ новые запросы мгновенно отклоняются ответом -32050 Server Overloaded + HTTP 503 Service Unavailable. Исключения: GET /health и GET /metrics (скрейп телеметрии полезен и при деградации).
Выполняющиеся в момент сбоя запросы завершаются штатно; их ошибки записи так же переводят флаг.
GET /health отвечает HTTP 503 пока db_healthy_ == false — оркестратор (Kubernetes liveness/readiness probe) быстро убивает под и переносит его на машину с рабочим диском. Это целевой fail-fast, а не попытка дожить на деградировавшем узле.
Автовосстановление внутри процесса ОТСУТСТВУЕТ: флаг сбрасывается только рестартом процесса. После рестарта движок либо успешно открывает БД (место освободилось), либо вновь критично завершается — третьего режима нет.

## 1.4. Реализация в C++ (Error Handling Flow)
Для предотвращения утечки исключений наружу (что может привести к падению процесса Drogon), бизнес-логика использует кастомные типы исключений или std::expected (C++23, или реализация через паттерн Result в C++20).

C++ Паттерн (Result Type):

Примечание по библиотекам: стек использует simdjson ИСКЛЮЧИТЕЛЬНО для парсинга входящих MCP-запросов (On-Demand DOM, docs/00 §1). Для построения исходящих JSON-RPC ответов и структур данных ошибок используется встроенный JSON-билдер Drogon (тип Json::Value / drogon::Json::Value).

cpp

struct RpcError {
    int code;
    std::string message;
    Json::Value data;  // Drogon JSON builder (drogon::Json::Value)
};

template<typename T>
using RpcResult = std::expected<T, RpcError>;
Пример в коде (Метод cast_vote):

cpp

RpcResult<VoteReceipt> ConsensusEngine::cast_vote(...) {
    auto membership = db_->get_membership(org_id, agent_id);
    if (!membership || membership->status != "ACTIVE") {
        return RpcError{-32002, "Forbidden", {{"reason", "Agent is not an active member"}}};
    }
    
    auto proposal = db_->get_proposal(org_id, proposal_id); // Изоляция: доступ к Proposal только через org_id (docs/03 §1.3.1)
    if (!proposal) {
        return RpcError{-32004, "Not Found", {{"reason", "Proposal not found"}}};
    }
    
    if (proposal->status != "ACTIVE") {
        return RpcError{-32003, "Conflict", {{"reason", "Proposal is closed"}, {"current_status", proposal->status}}};
    }
    // ... успешное выполнение
}
Слой MCP Handler (Drogon): перехватывает RpcResult. Если возвращается ошибка, хендлер формирует JSON-RPC Error Response и закрывает соединение с нужным HTTP-статусом. Это гарантирует, что вся логика консенсуса остается чистой и тестируемой изолированно от транспортного слоя.