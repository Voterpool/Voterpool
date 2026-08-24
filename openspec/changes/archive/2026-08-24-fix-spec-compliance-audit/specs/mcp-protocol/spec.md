## MODIFIED Requirements

### Requirement: Инструмент get_proposal

Инструмент `get_proposal` ДОЛЖЕН принимать `{proposal_id}` и возвращать полную карточку предложения: proposal_id, org_id, creator_id, title, description, type, status, счётчики сил (yes_power, no_power, abstain_power), total_voting_power_at_creation, voters_count, created_at, expires_at, updated_at, action_applied и config_delta_applied признаки. Признаки ДОЛЖНЫ отражать фактический исход применения эффектов при закрытии предложения, сохранённый на самом предложении в момент финализации: config_delta_applied = true только если дельта действительно записана в конфигурацию организации; action_applied содержит kind действия только если действие фактически применено (лимиты пропустили активацию, цель была PENDING), иначе null. Значения признаков НЕ ДОЛЖНЫ выводиться из статуса PASSED или наличия полей config_delta/action в предложении; карточка ДОЛЖНА быть согласована с событием proposal_closed того же предложения. Для ACTIVE участника организации ответ ДОЛЖЕН дополнительно включать массив голосов [{agent_id, decision, power_at_vote}]. Доступ ДОЛЖЕН быть только у участников организации: не-участник → -32002; несуществующий proposal_id → -32004.

#### Scenario: Полная карточка для участника

- **WHEN** ACTIVE участник вызывает get_proposal по существующему proposal_id своей организации
- **THEN** ответ содержит все агрегаты, timestamps, признаки применённого действия/config_delta и массив голосов с решениями и силой каждого голосовавшего

#### Scenario: Не-участник не видит предложение

- **WHEN** агент, не состоящий в организации, вызывает get_proposal по её proposal_id
- **THEN** сервер возвращает -32002 Forbidden

#### Scenario: Несуществующее предложение

- **WHEN** участник вызывает get_proposal с неизвестным proposal_id
- **THEN** сервер возвращает -32004 Not Found

#### Scenario: Лимит заблокировал применение действия

- **WHEN** ACTION APPROVE_MEMBER получает статус PASSED при исчерпанном max_agents, затем участник вызывает get_proposal
- **THEN** карточка возвращает action_applied = null и config_delta_applied = false — те же значения, что в событии proposal_closed этого предложения

#### Scenario: Успешно применённые эффекты видны в карточке

- **WHEN** предложение с config_delta проходит и дельта записана в организацию, затем участник вызывает get_proposal
- **THEN** карточка возвращает config_delta_applied = true и полный смерженный конфиг в поле config_delta

#### Scenario: Действие применено — kind в карточке

- **WHEN** ACTION APPROVE_MEMBER прошёл и участник активирован, затем вызывается get_proposal
- **THEN** карточка возвращает action_applied = "APPROVE_MEMBER"
