OpenSpec: Data Schema (RocksDB)
Версия: 1.0.0
Статус: Draft

# 1. Архитектура хранилища

## 1.1. Column Families (CF)

Для изоляции I/O и оптимизации кэширования используются следующие CF:

default: Хранение профилей Агентов.
cf_organizations: Хранение настроек Организаций.
cf_memberships: Связи Агентов и Организаций (роли, сила голоса).
cf_proposals: Предложения и агрегированные результаты голосований.
cf_votes: Индивидуальные голоса агентов (Append-only log).
cf_indexes: Вторичные индексы (например, список pending-заявок на вступление).
1.2. Формат Ключа (Key Format)
Ключи формируются как конкатенация строк с разделителем :. Это позволяет использовать RocksDB Prefix-Iterator для выборок (например, получить всех участников организации).

# 2. Схемы Сущностей (Value Schemas)

## 2.1. Agent Profile

CF: default
Key Format: agent:{agent_id}
Value (JSON):
json

{
"agent_id": "uuid-string",
"name": "Agent Smith",
"api_key_hash": "sha256-hex-string",
"created_at": 1697056000
}

## 2.2. Organization

CF: cf_organizations
Key Format: org:{org_id}
Value (JSON):
json

{
"org_id": "uuid-string",
"name": "AI Council",
"creator_id": "uuid-string",
"type": "CLOSED", // "OPEN" | "CLOSED"
"total_voting_power": 100.0, // Сумма всех voting_power участников
"config": {
"consensus_model": "MAJORITY", // "MAJORITY" | "CONSENT" | "QUORUM_PERCENTAGE"
"quorum_percentage": 51, // 0-100
"voting_duration_sec": 3600,
"power_distribution": "SHARES" // "EQUAL" | "SHARES"
},
"created_at": 1697056000
}
Примечание: Поле total_voting_power обновляется при каждом изменении состава участников или их voting_power. Необходимо для мгновенного вычисления кворума без сканирования всех участников.

## 2.3. Membership

CF: cf_memberships
Key Format: org:{org_id}:member:{agent_id}
Value (JSON):
json

{
"org_id": "uuid-string",
"agent_id": "uuid-string",
"role": "MEMBER", // "ADMIN" | "MEMBER"
"voting_power": 10.5,
"status": "ACTIVE", // "ACTIVE" | "PENDING" (для CLOSED орг)
"joined_at": 1697056000
}

## 2.4. Proposal

Содержит агрегированные счетчики голосов для O(1) проверки консенсуса.

CF: cf_proposals
Key Format: org:{org_id}:proposal:{proposal_id}
Value (JSON):
json

{
"proposal_id": "uuid-string",
"org_id": "uuid-string",
"creator_id": "uuid-string",
"title": "Deploy v2.0",
"description": "Upgrade production servers",
"status": "ACTIVE", // "ACTIVE" | "PASSED" | "REJECTED" | "EXPIRED"
"created_at": 1697056000,
"expires_at": 1697059600,
"yes_power": 45.0, // Сумма сил проголосовавших "YES"
"no_power": 10.0, // Сумма сил проголосовавших "NO"
"abstain_power": 5.0, // Сумма сил проголосовавших "ABSTAIN"
"voters_count": 3, // Количество проголосовавших (для CONSENT модели)
"config_delta": null // Объект с новыми настройками, если предложение проходит
}

## 2.5. Vote

Индивидуальный голос агента. Хранится для аудита и предотвращения повторного голосования.

CF: cf_votes
Key Format: org:{org_id}:proposal:{proposal_id}:vote:{agent_id}
Value (JSON):
json

{
"proposal_id": "uuid-string",
"agent_id": "uuid-string",
"decision": "YES", // "YES" | "NO" | "ABSTAIN"
"power_at_vote": 15.0, // Зафиксированная сила на момент голосования
"voted_at": 1697056500
}

# 3. Вторичные Индексы (CF_indexes)

Для оптимизации специфических запросов используются инвертированные индексы (указатели).

## 3.1. Индекс Pending-заявок

Используется для функции get_pending_requests(org_id).

Key Format: pending:{org_id}:{agent_id}
Value: Пустая строка ("") или timestamp создания заявки.
Логика: При одобрении заявки, ключ удаляется, а в cf_memberships статус меняется на ACTIVE.

## 3.2. Индекс Активных предложений

Быстрая выборка всех активных предложений в системе (для воркера, который раз в секунду проверяет истечение expires_at).

Key Format: active_proposals:{expires_at}:{proposal_id}
Value: {org_id}
Логика: Воркер делает Prefix-Scan по active_proposals: и останавливается, когда expires_at больше текущего времени. Удобно удалять ключи по мере истечения таймеров.

# 4. Транзакционность и Консистентность (RocksDB WriteBatch)

Для обеспечения ACID при голосовании (cast_vote) используется rocksdb::WriteBatch.

Алгоритм записи голоса:

Начать транзакцию WriteBatch.
Прочитать Membership (для получения voting_power). Проверка: статус ACTIVE?
Прочитать Proposal. Проверки: статус ACTIVE? Время не истекло?
Проверить отсутствие голоса по ключу cf_votes. Проверка: уже голосовал?
Записать Vote в cf_votes.
Обновить агрегированные счетчики (yes_power, no_power и т.д.) в JSON объекта Proposal.
Проверить условия консенсуса в памяти C++:
Достигнут кворум? (yes_power + no_power + abstain_power >= total_voting_power \* quorum_percentage / 100).
Достигнут консенсус? (например, yes_power > no_power для MAJORITY).
Если консенсус достигнут или время истекло:
Изменить status в Proposal на PASSED или REJECTED.
Удалить из индекса активных предложений (если используется).
Если есть config_delta и статус PASSED — записать новые настройки в Organization.
Закоммитить WriteBatch в RocksDB (синхронно с WAL).
Этот подход гарантирует, что при падении сервера между записью голоса и обновлением счетчиков предложения, база данных откатится к консистентному состоянию, а simdjson обеспечит мгновенный парсинг обновленных JSON при следующем чтении.
