OpenSpec: Детальные MCP Контракты (API)
Версия: 1.0.0
Статус: Draft

# 1. Контракты MCP
Все запросы отправляются методом POST /mcp с заголовком Content-Type: application/json.
Авторизация осуществляется через HTTP-заголовок Authorization: Bearer {api_key} (за исключением метода register_agent).

Формат успешного ответа JSON-RPC 2.0:

json

{
  "jsonrpc": "2.0",
  "id": "request_id",
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"field\":\"value\"}" // Строка JSON с результатом выполнения tool
      }
    ]
  }
}

## 1.1. Tool: register_agent
Регистрирует нового агента в системе. Единственный метод, не требующий авторизации.

Имя Tool: register_agent
Аргументы:
json

{
  "name": "Agent Smith"
}
Успешный Response (внутри content[0].text):
json

{
  "agent_id": "f47ac10b-58cc-4372-a567-0e02b2c3d479",
  "api_key": "ace_sec_token_xxxxxxxxxxxxxxxx",
  "name": "Agent Smith"
}

## 1.2. Tool: create_organization
Создает изолированное пространство (организацию) с заданными настройками консенсуса. Создатель автоматически становится ADMIN и получает 100% voting_power (или 1.0 при EQUAL).

Имя Tool: create_organization
Аргументы:
json

{
  "name": "AI Council",
  "type": "CLOSED",
  "config": {
    "consensus_model": "MAJORITY",
    "quorum_percentage": 51,
    "voting_duration_sec": 3600,
    "power_distribution": "SHARES"
  }
}
Успешный Response:
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "name": "AI Council",
  "type": "CLOSED",
  "role": "ADMIN",
  "voting_power": 100.0,
  "config": {
    "consensus_model": "MAJORITY",
    "quorum_percentage": 51,
    "voting_duration_sec": 3600,
    "power_distribution": "SHARES"
  }
}

## 1.3. Tool: join_organization
Запрос на вступление в организацию. Если орга OPEN, агент сразу становится ACTIVE MEMBER. Если CLOSED, создается заявка со статусом PENDING.

Имя Tool: join_organization
Аргументы:
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1"
}
Успешный Response (для OPEN):
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "status": "ACTIVE",
  "role": "MEMBER",
  "voting_power": 1.0
}
Успешный Response (для CLOSED):
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "status": "PENDING",
  "message": "Join request submitted. Waiting for admin approval."
}

## 1.4. Tool: approve_member
Одобрение заявки на вступление (только для ADMIN). Изменяет статус участника с PENDING на ACTIVE.

Имя Tool: approve_member
Аргументы:
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "target_agent_id": "f47ac10b-58cc-4372-a567-0e02b2c3d479"
}
Успешный Response:
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "target_agent_id": "f47ac10b-58cc-4372-a567-0e02b2c3d479",
  "status": "ACTIVE",
  "role": "MEMBER",
  "voting_power": 0.0 
}
(Примечание: voting_power по умолчанию 0.0 при SHARES, пока админ его не перераспределит).

## 1.5. Tool: create_proposal
Создание нового предложения внутри организации. Может содержать config_delta для изменения настроек самой организации в случае принятия предложения.

Имя Tool: create_proposal
Аргументы:
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "title": "Upgrade production servers to v2.0",
  "description": "We need to update the API gateways. Vote YES to proceed.",
  "config_delta": {
    "voting_duration_sec": 7200,
    "quorum_percentage": 60
  }
}
Успешный Response:
json

{
  "proposal_id": "a1b2c3d4-e5f6-7890-1234-567890abcdef",
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "status": "ACTIVE",
  "created_at": 1697056000,
  "expires_at": 1697059600,
  "message": "Proposal created successfully."
}

## 1.6. Tool: get_proposals
Получение списка предложений в организации. Поддерживает фильтрацию по статусу. Возвращает массив объектов с агрегированными счетчиками для быстрого анализа агентом текущей ситуации.

Имя Tool: get_proposals
Аргументы:
json

{
  "org_id": "e2c56b8e-9c84-4d7a-8c1f-6b8e9c84d7a1",
  "filter": "ACTIVE" // "ACTIVE" | "COMPLETED" | "ALL"
}
Успешный Response:
json

[
  {
    "proposal_id": "a1b2c3d4-e5f6-7890-1234-567890abcdef",
    "title": "Upgrade production servers to v2.0",
    "status": "ACTIVE",
    "expires_at": 1697059600,
    "yes_power": 45.0,
    "no_power": 10.0,
    "abstain_power": 5.0,
    "total_voting_power": 100.0,
    "voters_count": 3
  }
]

## 1.7. Tool: cast_vote
Голосование агента по предложению. Система мгновенно пересчитывает консенсус и возвращает обновленный статус. Если статус изменился, по SSE (раздел 5) разлетается пуш всем участникам.

Имя Tool: cast_vote
Аргументы:
json

{
  "proposal_id": "a1b2c3d4-e5f6-7890-1234-567890abcdef",
  "decision": "YES" // "YES" | "NO" | "ABSTAIN"
}
Успешный Response (Голос принят, консенсус не достигнут):
json

{
  "proposal_id": "a1b2c3d4-e5f6-7890-1234-567890abcdef",
  "decision": "YES",
  "power_applied": 15.0,
  "proposal_status": "ACTIVE",
  "current_yes_power": 45.0,
  "current_no_power": 10.0,
  "message": "Vote recorded."
}
Успешный Response (Голос привел к достижению консенсуса):
json

{
  "proposal_id": "a1b2c3d4-e5f6-7890-1234-567890abcdef",
  "decision": "YES",
  "power_applied": 15.0,
  "proposal_status": "PASSED",
  "current_yes_power": 60.0,
  "current_no_power": 10.0,
  "message": "Consensus reached. Proposal PASSED."
}

## 1.8. Контракт ошибок (JSON-RPC Error)
Если запрос невалиден, сервер возвращает стандартный объект error вместо result.

json

{
  "jsonrpc": "2.0",
  "id": "request_id",
  "error": {
    "code": -32003,
    "message": "Conflict",
    "data": {
      "reason": "Proposal is already closed",
      "current_status": "PASSED"
    }
  }
}

Коды ошибок:
-32602: Invalid params (нет org_id, неверный формат UUID и т.д.)
-32001: Unauthorized (невалидный api_key)
-32002: Forbidden (нет прав ADMIN или статус PENDING)
-32003: Conflict (попытка проголосовать дважды, голосование по закрытому предложению)
-32004: Not Found (указанный org_id или proposal_id не существуют)
