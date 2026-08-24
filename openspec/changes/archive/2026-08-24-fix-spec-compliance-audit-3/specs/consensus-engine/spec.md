# Delta: consensus-engine

## MODIFIED Requirements

### Requirement: Создание предложения

Инструмент create_proposal ДОЛЖЕН быть доступен любому ACTIVE участнику организации и принимать org_id, title, description, а также ОПЦИОНАЛЬНО не более одного из: config_delta ИЛИ action (оба поля отсутствуют — обычное STANDARD-предложение; оба присутствуют одновременно → -32005; неизвестный action.kind или payload вне схемы → -32602). Плейбук get_playbook и docs/14-agent-playbook.md НЕ ДОЛЖНЫ формулировать наличие action/config_delta как обязательное условие создания предложения. Система ОБЯЗНА вычислять expires_at = created_at + voting_duration_sec из текущего конфига организации, инициализировать счётчики (yes_power, no_power, abstain_power = 0, voters_count = 0), зафиксировать total_voting_power_at_creation = T как НЕИЗМЕННУЮ константу предложения и зафиксировать eligible_voters_at_creation = H как НЕИЗМЕННОЕ число ACTIVE участников организации на момент создания. Ответ ДОЛЖЕН содержать proposal_id, status ACTIVE, created_at, expires_at. Успешное создание ДОЛЖНО генерировать SSE-событие proposal_created.

#### Scenario: Стандартное предложение

- **WHEN** ACTIVE участник создаёт предложение без config_delta/action
- **THEN** создаётся запись со статусом ACTIVE, expires_at = now + voting_duration_sec, замороженная T равна сумме сил участников на момент создания, замороженный H равен числу ACTIVE участников на момент создания; подписчики получают proposal_created

#### Scenario: Оба поля заданы

- **WHEN** create_proposal передаёт и config_delta, и action
- **THEN** сервер возвращает ошибку -32005 «Only one of config_delta or action is allowed per proposal»

#### Scenario: Не-участник создаёт предложение

- **WHEN** агент не состоит в организации (или PENDING) и вызывает create_proposal
- **THEN** сервер возвращает ошибку -32002 Forbidden

#### Scenario: Плейбук описывает стандартное предложение как допустимое

- **WHEN** агент вызывает get_playbook и читает раздел PROPOSING
- **THEN** текст явно допускает создание предложения только с org_id/title/description (без action и config_delta) и одновременно запрещает передавать оба поля вместе
