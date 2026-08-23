# Fix Consensus Finalize Atomicity

## Why

`ConsensusEngine::finalizeLocked` применяет эффекты PASSED-предложения неатомарно: запись организации выполняется через `OrgRepository::put(const Organization&)`, который создаёт **собственный** WriteBatch и коммитит немедленно, тогда как статус предложения, индексы и аудит ждут батча вызывающего (`castVote`, `closeProposalByTimer`). Сбой второго коммита или краш между коммитами оставляет частичное состояние — прямое нарушение `data-persistence` («каждая составная мутация… единая атомарная транзакцией») и `consensus-engine` («в одной транзакции закрытия», «переиндексация атомарно в той же транзакции»). Худший сценарий: при APPROVE_MEMBER инкремент `total_voting_power` уже долговечен, но membership остался PENDING (батч упал) — предложение остаётся ACTIVE, повторное закрытие повторно активирует участника и **двойно инкрементирует** voting_power. Для UPDATE_ORG_INFO организация обновлена, а поисковые индексы (name/tags/category) остались старыми.

## What Changes

- Новая перегрузка `OrgRepository::put(rocksdb::WriteBatch&, const Organization&)` — зеркало существующей пары `putMembership` (batch / standalone).
- Три замены в `finalizeLocked`: прямые вызовы `d_.orgs->put(...)` (config_delta, APPROVE_MEMBER total_voting_power, UPDATE_ORG_INFO) переходят на batch-вариант. Один коммит покрывает proposal + индексы + аудит + организацию; промежуточное состояние конструктивно невозможно.
- Документация и спецификации НЕ меняются — они уже требуют атомарность; чинится реализация.
- Тесты:
  - расширение `test_storage_repos`: batch-перегрузка пишет в CF только после commit;
  - интеграционный тест отказа: инжекция сбоя `commit` после finalize → ни предложение, ни организация, ни индексы, ни аудит не изменены; повторное закрытие применяет эффект ровно один раз;
  - регресс на двойной инкремент `total_voting_power` при APPROVE_MEMBER после симулированного сбоя первого закрытия;
  - e2e: config_delta-предложение проходит полный цикл с консистентными org-профилем и аудитом.

## Capabilities

### New Capabilities

(нет)

### Modified Capabilities

(нет) — требования атомарности уже зафиксированы в `data-persistence` и `consensus-engine`; изменение приводит код в соответствие с ними, поведение требований не меняет. Поэтому изменение объявляет `skip_specs: true`.

## Impact

- `include/storage/repositories/OrgRepository.h` / `src/storage/repositories/OrgRepository.cpp` — новая batch-перегрузка put.
- `src/consensus/ConsensusEngine.cpp` — три точки записи организации в `finalizeLocked`.
- Паттерн уже доказан в кодовой базе: `db_putOrg(app, batch, …)` (ToolHelpers.h) используется всеми тулзами, включая атомарный роспуск (`DissolveOrg`); исправляется последний обходной путь.
- `tests/integration/` — тесты отказа и идемпотентности; существующие сценарии cast_vote_tx должны пройти без изменений.
