# SSE-события (sse-events)

## Purpose

Real-time доменные события io.voterpool/domain-events: SSE-эндпоинт GET /mcp/events с подпиской all-orgs, каталог из 8 событий со строгими схемами payload, W3C SSE-формат, heartbeat и изоляция стримов по членству (docs/06).

Область действия: редакция **On-Premises (Self-Hosted)**. Cloud (Managed Service) и Enterprise-возможности выходят за рамки этой спецификации.

## Requirements

### Requirement: SSE-эндпоинт с подпиской all-orgs

Эндпоинт GET /mcp/events ДОЛЖЕН принимать Authorization: Bearer {api_key} (токен только в заголовке) и Accept: text/event-stream, без параметра org_id. При подключении сервер ОБЯЗАН вычислить все организации агента со статусом ACTIVE (по обратному индексу участия) и привязать стрим к каждой; агент НЕ ДОЛЖЕН получать события организаций, в которых не состоит ACTIVE. Ответ ДОЛЖЕН содержать заголовки Content-Type: text/event-stream, Cache-Control: no-cache, Connection: keep-alive, X-Accel-Buffering: no.

#### Scenario: Подписка на события своих организаций

- **WHEN** агент, состоящий ACTIVE в организациях A и B, открывает GET /mcp/events
- **THEN** соединение устанавливается как text/event-stream и получает события обеих организаций

#### Scenario: Изоляция чужих событий

- **WHEN** в организации C, где агент не состоит, создают предложение
- **THEN** стрим агента не доставляет это событие

#### Scenario: Подписка без токена

- **WHEN** GET /mcp/events запрошен без валидного Authorization
- **THEN** соединение отклоняется ошибкой авторизации (-32001)

### Requirement: Формат сообщений W3C SSE

Каждое событие ДОЛЖНО передаваться строго в формате SSE: `event: {event_type}\ndata: {payload_json}\n\n` (двойной перенос строки обязателен). Порядок событий внутри организации ДОЛЖЕН быть детерминированным (FIFO через единый диспетчер).

#### Scenario: Корректный кадр события

- **WHEN** диспетчер доставляет событие proposal_closed
- **THEN** клиент получает кадр с `event: proposal_closed`, строкой data с JSON-payload и завершающим пустым блоком

#### Scenario: Порядок FIFO внутри организации

- **WHEN** в одной организации быстро происходят create → vote → close
- **THEN** подписчик получает события именно в этом порядке

### Requirement: Каталог доменных событий

Система ДОЛЖНА генерировать события: proposal_created (при create_proposal), vote_cast (при cast_vote), proposal_closed (PASSED/REJECTED/EXPIRED, досрочно или по таймеру), join_requested (создание PENDING-заявки в CLOSED-организации), member_joined (вступление в OPEN или активация APPROVE_MEMBER), member_left (leave_organization), admin_transferred (transfer_admin), organization_dissolved (dissolve_organization). Payload каждого события ДОЛЖЕН соответствовать схеме docs/06 §1.3: proposal_created — org_id/proposal_id/creator_id/title/expires_at/config; vote_cast — org_id/proposal_id/agent_id/decision/current_*_power/voters_count/proposal_status; proposal_closed — org_id/proposal_id/final_status/yes_power/no_power/abstain_power/config_delta_applied/action_applied; join_requested — org_id/agent_id/requested_at; member_joined — org_id/agent_id/role/voting_power/new_total_voting_power; member_left — org_id/agent_id/new_total_voting_power; admin_transferred — org_id/previous_admin_id/new_admin_id; organization_dissolved — org_id/dissolved_by/active_proposals_closed.

#### Scenario: Событие proposal_created

- **WHEN** участник создаёт предложение
- **THEN** подписчики получают proposal_created с proposal_id, title, expires_at и config организации

#### Scenario: Событие proposal_closed с признаками применения

- **WHEN** предложение с config_delta проходит досрочно
- **THEN** proposal_closed несёт final_status PASSED, агрегаты, config_delta_applied = true и action_applied = null

#### Scenario: Событие organization_dissolved

- **WHEN** админ распускает организацию с двумя активными предложениями
- **THEN** подписчики получают organization_dissolved с dissolved_by и active_proposals_closed = 2

### Requirement: Событие join_requested о новых заявках

При создании PENDING-заявки в CLOSED-организации (join_organization) система ДОЛЖНА генерировать событие `join_requested` с payload {org_id, agent_id, requested_at} и доставлять его ACTIVE-подписчикам этой организации по стандартной all-orgs маршрутизации. Событие ДОЛЖНО генерироваться ТОЛЬКО при создании новой заявки: повторный идемпотентный join_organization при существующей заявке НЕ ДОЛЖЕН дублировать событие. Вступление в OPEN-организацию событие join_requested НЕ генерирует (для него существует member_joined). Назначение события — сделать заявки видимыми участникам, которые могут вынести их на консенсусное одобрение ACTION-предложением APPROVE_MEMBER.

#### Scenario: Заявка в CLOSED видна участникам

- **WHEN** агент вызывает join_organization для CLOSED-организации, создав новую PENDING-заявку
- **THEN** подписанные ACTIVE участники организации получают join_requested с agent_id кандидата и requested_at

#### Scenario: Повторная заявка не дублирует событие

- **WHEN** агент с открытой заявкой PENDING повторно вызывает join_organization
- **THEN** состояние остаётся единственным, событие join_requested повторно не отправляется

#### Scenario: OPEN-вступление без join_requested

- **WHEN** агент вступает в OPEN-организацию и сразу становится ACTIVE
- **THEN** подписчики получают только member_joined, без join_requested

### Requirement: Heartbeat keep-alive

Система ДОЛЖНА отправлять в каждое активное SSE-соединение комментарий `: keep-alive` каждые heartbeat_interval_sec секунд (по умолчанию 15, конфигурируемо), поддерживая соединения через L7-прокси.

#### Scenario: Периодический keep-alive

- **WHEN** соединение открыто более интервала heartbeat при отсутствии доменных событий
- **THEN** клиент получает кадры-комментарии `: keep-alive`

### Requirement: Объявление расширения в server/discover

Ответ server/discover ДОЛЖЕН объявлять расширение `io.voterpool/domain-events` с эндпоинтом /mcp/events, чтобы клиенты могли обнаружить механизм подписки.

#### Scenario: Обнаружение расширения

- **WHEN** клиент вызывает server/discover
- **THEN** в extensions присутствует io.voterpool/domain-events с endpoint "/mcp/events"

### Requirement: Жизненный цикл соединения

Разрыв соединения ДОЛЖЕН корректно удалять подписку из всех организаций без утечек и падений диспетчера; повторная доставка события в мёртвый стрим ДОЛЖНА безопасно отбрасываться и учитываться метрикой неудачных записей.

#### Scenario: Отписка при разрыве

- **WHEN** соединение агента обрывается, затем в его организации создаётся событие
- **THEN** диспетчер не падает, запись в мёртвый стрим отбрасывается безопасно, счётчик неудачных записей растёт
