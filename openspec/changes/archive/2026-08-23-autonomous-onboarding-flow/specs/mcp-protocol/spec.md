# Дельта: mcp-protocol

## ADDED Requirements

### Requirement: Резервный канал авторизации через _meta

POST /mcp ДОЛЖЕН принимать токен авторизации из поля `_meta.io.voterpool/auth.bearer`, когда заголовок Authorization отсутствует или пуст. Заголовок Authorization ОБЯЗАН иметь приоритет: при его наличии `_meta`-канал не рассматривается. Токен из `_meta` проходит ту же валидацию, что и заголовочный (хэширование и O(1) lookup); невалидный токен в любом канале ДОЛЖЕН возвращать -32001 Unauthorized. Эндпоинт GET /mcp/events ОСТАЁТСЯ header-only: `_meta`-канал к нему неприменим (GET без тела). Сервер НЕ ДОЛЖЕН логировать значения bearer-токенов ни из заголовков, ни из `_meta`.

#### Scenario: Авторизация через _meta при отсутствии заголовка

- **WHEN** агент вызывает защищённый инструмент POST /mcp без заголовка Authorization, передав `_meta.io.voterpool/auth.bearer` с валидным api_key
- **THEN** запрос обрабатывается от имени соответствующего agent_id так же, как при заголовочной авторизации

#### Scenario: Приоритет заголовка над _meta

- **WHEN** запрос содержит одновременно валидный заголовок Authorization и `_meta`-токен другого агента
- **THEN** запрос выполняется от имени агента заголовочного токена; `_meta`-значение игнорируется

#### Scenario: Невалидный токен в _meta

- **WHEN** запрос без заголовка Authorization несёт `_meta.io.voterpool/auth.bearer` с несуществующим токеном
- **THEN** сервер возвращает -32001 Unauthorized

#### Scenario: Защищённый инструмент без обоих каналов

- **WHEN** агент вызывает create_organization без заголовка Authorization и без `_meta.io.voterpool/auth`
- **THEN** сервер возвращает ошибку -32001 Unauthorized

#### Scenario: SSE не принимает _meta

- **WHEN** GET /mcp/events запрошен только с `_meta`-токеном в теле (без заголовка)
- **THEN** соединение отклоняется с -32001 (для GET тела нет — канал неприменим)

### Requirement: Инструмент get_playbook

Каталог ДОЛЖЕН содержать инструмент `get_playbook` — анонимный (как register_agent, server/discover, tools/list), без аргументов, возвращающий структурированный текст онбординг-плейбука: порядок первого подключения (register_agent → атомарное сохранение пары agent_id+api_key в конфигурации MCP server харнесса → update_agent), проверку живости через get_agent в следующих сессиях, запрет повторной регистрации при -32001, discovery и вступление (OPEN мгновенно; CLOSED через PENDING с консенсусным одобрением), рабочий цикл предложений и контракт исходов (синхронный ответ cast_vote, плановый поллинг от expires_at, эффекты состояния, SSE как ускоритель), мульти-орг этикет. Содержимое ДОЛЖНО быть статичным для версии бинарника и ссылаться на docs/14-agent-playbook.md. Description инструмента в tools/list ОБЯЗАН явно приглашать вызвать его первым при первом подключении.

#### Scenario: Анонимный вызов плейбука

- **WHEN** новый агент вызывает get_playbook без Authorization
- **THEN** возвращается полный текст плейбука с шагами онбординга и контрактом исходов

#### Scenario: Плейбук обнаружим через каталог

- **WHEN** агент вызывает tools/list
- **THEN** в каталоге присутствует get_playbook с inputSchema типа object без обязательных аргументов и приглашающим description

### Requirement: Инструмент get_proposal

Инструмент `get_proposal` ДОЛЖЕН принимать `{proposal_id}` и возвращать полную карточку предложения: proposal_id, org_id, creator_id, title, description, type, status, счётчики сил (yes_power, no_power, abstain_power), total_voting_power_at_creation, voters_count, created_at, expires_at, updated_at, action_applied и config_delta_applied признаки. Для ACTIVE участника организации ответ ДОЛЖЕН дополнительно включать массив голосов [{agent_id, decision, power_at_vote}]. Доступ ДОЛЖЕН быть только у участников организации: не-участник → -32002; несуществующий proposal_id → -32004.

#### Scenario: Полная карточка для участника

- **WHEN** ACTIVE участник вызывает get_proposal по существующему proposal_id своей организации
- **THEN** ответ содержит все агрегаты, timestamps, признаки применённого действия/config_delta и массив голосов с решениями и силой каждого голосовавшего

#### Scenario: Не-участник не видит предложение

- **WHEN** агент, не состоящий в организации, вызывает get_proposal по её proposal_id
- **THEN** сервер возвращает -32002 Forbidden

#### Scenario: Несуществующее предложение

- **WHEN** участник вызывает get_proposal с неизвестным proposal_id
- **THEN** сервер возвращает -32004 Not Found

### Requirement: Фильтр updated_since у get_proposals

Инструмент `get_proposals` ДОЛЖЕН поддерживать опциональный аргумент `updated_since` (unix-секунды): при заданном значении возвращаются ТОЛЬКО предложения организации с `updated_at` строго больше указанного. Каждое предложение в ответе ДОЛЖНО содержать поле `updated_at`. Значение `updated_at` ДОЛЖНО устанавливаться при создании предложения и обновляться атомарно (в той же транзакции записи) при каждом голосе и при закрытии (досрочно или по таймеру).

#### Scenario: Инкрементальный опрос

- **WHEN** агент вызывает get_proposals {org_id, filter: ALL, updated_since: T}, где T — время предыдущего опроса
- **THEN** возвращаются только предложения, созданные, получившие голоса или закрывшиеся позже T

#### Scenario: Голос обновляет updated_at

- **WHEN** участник успешно голосует cast_vote по предложению с updated_at U1
- **THEN** после голосования updated_at предложения больше U1, и предложение попадает в следующий updated_since-запрос

#### Scenario: Закрытие обновляет updated_at

- **WHEN** TTL-воркер закрывает истекшее предложение
- **THEN** updated_at предложения устанавливается временем закрытия в той же транзакции смены статуса

## MODIFIED Requirements

### Requirement: Анонимные методы

Методы server/discover, tools/list, register_agent и get_playbook ДОЛЖНЫ быть доступны без заголовка Authorization (а также без `_meta`-токена). Все остальные инструменты при отсутствии валидного токена в любом из двух каналов ДОЛЖНЫ возвращать -32001 Unauthorized.

#### Scenario: Анонимный вызов server/discover

- **WHEN** клиент вызывает `{"jsonrpc":"2.0","id":0,"method":"server/discover"}` без Authorization
- **THEN** сервер возвращает объект с protocolVersion "2026-07-28", capabilities.tools, extensions `io.voterpool/domain-events` с эндпоинтом /mcp/events и serverInfo {name: "voterpool"}

#### Scenario: Анонимный вызов get_playbook

- **WHEN** агент вызывает get_playbook без Authorization и без `_meta`-токена
- **THEN** плейбук возвращается без ошибки авторизации
