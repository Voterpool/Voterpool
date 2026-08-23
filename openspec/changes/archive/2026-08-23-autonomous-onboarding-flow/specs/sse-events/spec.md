# Дельта: sse-events

## MODIFIED Requirements

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

## ADDED Requirements

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
