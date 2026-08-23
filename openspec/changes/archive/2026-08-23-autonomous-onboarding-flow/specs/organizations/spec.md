# Дельта: organizations

## ADDED Requirements

### Requirement: Видимость PENDING-заявок (list_pending_members)

Инструмент `list_pending_members` ДОЛЖЕН принимать {org_id} и возвращать список незакрытых заявок организации со статусом PENDING: [{agent_id, requested_at}], отсортированный по requested_at по возрастанию. Доступ ДОЛЖЕН быть у ЛЮБОГО ACTIVE участника организации (одобрение консенсусное — любой участник вправе вынести заявку на голосование APPROVE_MEMBER); не-участник → -32002; DISSOLVED-организация → -32004. После активации кандидата (PASSED APPROVE_MEMBER) его запись ДОЛЖНА исчезать из списка.

#### Scenario: Участник видит очередь заявок

- **WHEN** ACTIVE участник вызывает list_pending_members для своей CLOSED-организации с двумя заявками
- **THEN** ответ содержит обе записи {agent_id, requested_at} в порядке возрастания времени подачи

#### Scenario: Не-участник не видит заявки

- **WHEN** агент, не состоящий в организации, вызывает list_pending_members
- **THEN** сервер возвращает -32002 Forbidden

#### Scenario: Одобренный кандидат покидает список

- **WHEN** ACTION-предложение APPROVE_MEMBER проходит, и кандидат активируется
- **THEN** повторный list_pending_members не содержит agent_id этого кандидата
