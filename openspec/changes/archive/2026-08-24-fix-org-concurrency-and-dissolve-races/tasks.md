## 1. Подготовка и примитивы

- [x] 1.1 Обобщить `ProposalLockRegistry` в `KeyedMutexRegistry` (include/consensus/ProposalLock.h): переименование класса с сохранением API acquire/forget/Guard, добавить алиас `using ProposalLockRegistry = KeyedMutexRegistry;`; обновить комментарий контракта (forget строго после успешного коммита терминального статуса; иерархия proposal → organization). Проверка: `cmake --build build && ctest -R "^ut\."` — юнит-тесты реестра зелёные без правок тестов.
- [x] 1.2 Добавить в `AppContext` (include/server/AppContext.h, src/server/AppContext.cpp) поле `KeyedMutexRegistry orgLocks`. Проверка: сборка проекта проходит (`cmake --build build`).
- [x] 1.3 Расширить tests/unit/test_proposal_lock.cpp кейсами на два независимых экземпляра реестра (proposal/org не конфликтуют ключами). Проверка: `ctest -R "test_proposal_lock"` зелёный.

## 2. Fix №1 — роспуск: лок до коммита

- [x] 2.1 Реализовать `ConsensusEngine::dissolveOrganization(orgId)` по алгоритму D3 (design.md): список кандидатов → Guard'ы в отсортированном порядке → org-лок → единый WriteBatch (EXPIRED + removeActiveIndex + Organization DISSOLVED + чистка feed/name/tags/category + аудит ORG_DISSOLVED) → commit → forget() только после успешного коммита → зачистка отставших ограниченным числом проходов. Обновить заголовок include/consensus/ConsensusEngine.h. Проверка: сборка зелёная.
- [x] 2.2 Перевести src/mcp/tools/DissolveOrg.cpp на `dissolveOrganization`: в тулзе остаются авторизация ADMIN, SSE-доставка (proposal_closed × N + organization_dissolved), метрики и refreshOrgGauges; удалить старый вызов expireAllForDissolve. Проверка: существующие e2e/integration тесты dissolve проходят (`ctest -R "Dissolve|dissolve"`).
- [x] 2.3 Добавить проверку статуса организации внутри критической секции castVote (src/consensus/ConsensusEngine.cpp): DISSOLVED → ошибка -32004 после захвата proposal-лока и перечитывания предложения. Проверка: `ctest -R "Concurrency|CastVote"` зелёный.

## 3. Fix №2 — org-level сериализация мутаций

- [x] 3.1 JoinOrg.cpp OPEN-ветка: обернуть «чтение организации → проверки max_agents/joins_per_day_limit → батч (putMembership + incrementJoinLimit + db_putOrg) → commit» в org-лок. Проверка: `ctest -R "JoinOrg|Onboarding"` зелёный.
- [x] 3.2 UpdateVotingPower.cpp: org-лок вокруг чтения организации/членства, проверки ≤100%, сборки батча и коммита. Проверка: `ctest -R "VotingPower|Actions"` зелёный.
- [x] 3.3 OrgGovernance.cpp: org-лок для leave_organization (включая подсчёт админов и декремент total_voting_power) и transfer_admin. Проверка: `ctest -R "Governance"` зелёный.
- [x] 3.4 CreateProposal.cpp: org-лок вокруг проверки статуса, снимков T/H и вставки предложения. Проверка: `ctest -R "Proposal"` зелёный.
- [x] 3.5 ConsensusEngine.cpp `finalizeLocked`: ветви config_delta / APPROVE_MEMBER / UPDATE_ORG_INFO выполняются под org-локом (порядок proposal → organization); при DISSOLVED эффекты пропускаются (configDeltaApplied=false, actionApplied=false). Аудит всех вызователей `db_putOrg`/`putMembership`/`incrementJoinLimit` — убедиться, что вне org-лока мутаций организации не осталось (grep). Проверка: `ctest -R "Actions|ConsentFlow|TtlWorker"` зелёный.

## 4. Регрессионные тесты гонок

- [x] 4.1 tests/integration/test_concurrency.cpp: тест «ConcurrentJoinsRespectLimitsAndTotals» — N потоков одновременно join_organization в OPEN-орг с joins_per_day_limit = K < N; assert: ровно K успехов (-32005 у остальных), счётчик лимита = K, total_voting_power вырос ровно на K, число ACTIVE участников +K. Запустить 20+ итераций.
- [x] 4.2 Тест «ConcurrentOrgMutationsLoseNoUpdates»: параллельные mix-операции (join × leave × update_voting_power на SHARES-орге); assert: итоговый total_voting_power == сумма voting_power ACTIVE участников, капа 100% не нарушена, ошибки только ожидаемых кодов.
- [x] 4.3 Тест «VoteRacesDissolveExactlyOneOutcome» (30 итераций как VoteVsTimerClose…): cast_vote concurrently с dissolve_organization; assert: предложение закрыто ровно один раз (EXPIRED), голос либо учтён в счётчиках EXPIRED-записи, либо отклонён (-32003/-32004), повторный cast_vote после dissolve → ошибка, сумма счётчиков == сумме power_at_vote зафиксированных голосов, config_delta/ACTION не применены к распущенной организации.
- [x] 4.4 Тест «CreateProposalRacesDissolve»: create_proposal concurrently с dissolve; assert: предложение либо отклонено -32004, либо закрыто EXPIRED (немедленной зачисткой или TTL-проходом closeExpired), в распущенной организации нет живых ACTIVE предложений после зачистки.
- [x] 4.5 Зарегистрировать новые файлы/тесты в tests/CMakeLists.txt (если создаются новые файлы). Проверка: `ctest` обнаруживает все новые тесты. (Новые файлы не создавались — тесты добавлены в существующий test_concurrency.cpp; ctest обнаруживает #77–#80.)

## 5. Прогон и синхронизация docs

- [x] 5.1 Полный прогон: `cmake --build build && ctest --output-on-failure` — все unit/integration/e2e тесты зелёные, включая новые (135/135 passed); результат зафиксирован в итоговом резюме сессии применения.
- [x] 5.2 Синхронизировать docs: docs/01-data-scheme.md §5.2–5.3 (KeyedMutexRegistry, иерархия proposal → organization, правило «под org-локом нет ожидания proposal-лока», контракт forget-after-commit для dissolve), docs/04-background-workers.md (инвариант перевыпуска лока — дополнить путь роспуска), docs/05-mcp-contracts.md §join_organization/dissolve_organization (семантика конкурентных вступлений и пост-роспускных операций). Проверка: grep по упомянутым разделам не находит устаревших формулировок («ровно один proposal-замок на операцию»).
