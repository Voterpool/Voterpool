## Why

docs/16 v2.0.0 (Этап 1, Стратегия 2) фиксирует: блокеры шардирования SH-1..SH-4 закрытываются только внутренними швами — плоскости Directory/Identity/Event прячутся за компайл-тайм порты. Сегодня call-site'ы (AuthMiddleware, SearchOrganizations, AgentProfile, SseHub) напрямую трогают глобальные keyspaces и реестры — при выносе в кластер каждый из них стал бы точкой переписывания. Порты дают это будущее через конфигурацию, сохраняя одиночную self-hosted ноду без изменений.

## What Changes

- Добавляются три порта в `include/scaling/`: `IDirectory` (поиск каталога: имена, теги, категории, лента ACTIVE, proposal_lookup), `IIdentity` (резолв токена, профиль агента, `agent_orgs`, регистрация), `IEventBus` (подписки SSE all-orgs).
- Добавляются локальные имплементации `LocalDirectory` / `LocalIdentity` / `LocalEventBus` поверх существующих репозиториев, OrgNameRegistry и SseHub — та же RocksDB, та же семантика.
- Все call-site'ы переводятся на порты; прямые обращения к OrgNameRegistry и auth-пути AgentRepository вне имплов устраняются. Consensus-plane данные (membership/proposal/vote/audit/pending/join_limit) остаются прямыми вызовами — они не меняются.
- Конфигурация: секция `cluster` с единственным допустимым значением `mode: standalone` (значения кроме standalone → fail-fast при старте, exit 1). Задел под Этап 3.
- Контрактные тесты портов + фейки (`FakeIdentity` и др.) для тестовой сборки.
- **BREAKING**: отсутствует — внешнее поведение (инструменты MCP, форматы, коды ошибок, эндпоинты) не меняется; проверяется golden-регрессией.

## Capabilities

### New Capabilities

- `shard-seams`: контракт швов масштабируемости — обязательность прохода через IDirectory/IIdentity/IEventBus для плоскостей Directory/Identity/Event, семантика локальных имплементаций (эквивалентность текущему поведению), нейтральность внешнего API и правила конфигурации cluster.mode на Этапе 1 (docs/16 §3.1).

### Modified Capabilities

_(нет — поведение существующих способностей не меняется)_

## Impact

- Код: новые `include/scaling/*.h`, `src/scaling/*`; правки call-site'ов в `src/server/AuthMiddleware.cpp`, `src/mcp/tools/{SearchOrganizations,AgentProfile,RegisterAgent,JoinOrg,LeaveOrg}.cpp`, `src/server/AppContext.cpp`, `src/server/SseHub.cpp` (точки подписки); CMakeLists.
- Документы: docs/09 (дерево проекта — scaling/), docs/16 (статус Этапа 1), возможно docs/00 NFR-3 (ссылка на швы).
- Предусловие следующего этапа: change `add-bucket-keyspace` (Этап 2, Стратегия 3) стартует только после мержа этого change.
- Риски: протечка абстракции (порты шире нужного/уже нужного) — купируется контрактными тестами и golden-регрессией; производительность косвенного вызова — виртуальный вызов на пути запроса, замеряется бенчмарком до/после (порог: <2% на p99).
