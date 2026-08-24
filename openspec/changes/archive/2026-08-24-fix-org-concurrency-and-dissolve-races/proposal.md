## Why

Аудит соответствия specs ↔ код выявил две критические гонки, нарушающие требования `data-persistence` (атомарность составных мутаций, контракт per-proposal локов) и `organizations` (лимиты вступления, инкрементальный пересчёт total_voting_power, финальность роспуска):

1. **Роспуск отпускает per-proposal lock до коммита.** `ConsensusEngine::expireAllForDissolve` (src/consensus/ConsensusEngine.cpp:343) пишет EXPIRED в ещё не закоммиченный WriteBatch и на каждой итерации вызывает `locks->forget()` и разрушает Guard; батч коммитится только после цикла в DissolveOrg.cpp:44. Это нарушает контракт реестра (include/consensus/ProposalLock.h:10–20: forget — только после успешного коммита терминального статуса). После forget() конкурентный cast_vote создаёт новый мьютекс, читает из БД всё ещё ACTIVE-предложение (проверки статуса организации в castVote нет) и может воскресить предложение голосом после DISSOLVED либо применить ACTION/config_delta, которые dissolve запрещает.

2. **Потерянные обновления total_voting_power и лимитов при конкурентных мутациях организации.** JoinOrg.cpp:63–70 (OPEN-ветка), UpdateVotingPower.cpp:57–63, OrgGovernance.cpp:38–41 и ветви APPROVE_MEMBER/UPDATE_ORG_INFO в `finalizeLocked` выполняют read-modify-write записи Organization без org-level блокировки — last-write-wins теряет инкременты; `IndexRepository::incrementJoinLimit` (src/storage/repositories/IndexRepository.cpp:132) сам является незащищённым RMW. Два параллельных join_organization при лимите 1 оба проходят проверку (оба читают used=0) и оба пишут total = old+1 → лимит превышен, счётчик занижен, замороженная T будущих предложений занижена.

## What Changes

- Вводится org-level keyed-mutex реестр (обобщение `ProposalLockRegistry`); все read-check-write-commit мутации организации сериализуются по org_id.
- `expireAllForDissolve` реструктурируется: Guard'ы proposal-локов собираются до сборки батча и живут до успешного коммита; `forget()` вызывается строго после коммита терминального EXPIRED (восстановление контракта).
- cast_vote перепроверяет статус организации внутри критической секции предложения: DISSOLVED → -32004; применение эффектов config_delta/ACTION при уже распущенной организации пропускается.
- Лимиты max_agents / joins_per_day_limit и инкремент total_voting_power при вступлении/активации становятся атомарными относительно конкурентных вступлений, выходов, смен силы и роспуска.
- Дельта-спецификации для data-persistence и организаций + регрессионные тесты на обе гонки (конкурентные join/leave/update_voting_power/dissolve vs cast_vote).

## Capabilities

### New Capabilities

(нет — изменения касаются существующих возможностей)

### Modified Capabilities

- `data-persistence`: требование «Конкурентный контроль per-proposal» дополняется явным контрактом удержания лока до коммита терминального статуса (включая путь dissolve); новое требование о сериализации составных мутаций организации по org_id с фиксированным порядком локов (proposal → organization).
- `organizations`: требования «Вступление в OPEN-организацию», «Роли и распределение силы голоса» и «Роспуск организации без удаления» дополняются сценариями конкурентности: ровно одно вступление при исчерпанном лимите, ноль потерянных обновлений total_voting_power, невозможность голосовать/применять действия в распущенной организации.

## Impact

- Код: include/server/AppContext.h, include/consensus/ProposalLock.h (обобщение реестра), src/consensus/ConsensusEngine.cpp (`expireAllForDissolve`, `castVote`, `closeProposalByTimer`, `finalizeLocked`), src/mcp/tools/{JoinOrg,UpdateVotingPower,OrgGovernance,DissolveOrg,CreateProposal}.cpp.
- Контракты MCP не меняются (коды ошибок -32003/-32004/-32005 используются как объявлено); поведение в гонках становится детерминированным.
- Тесты: tests/integration/test_concurrency.cpp (новые сценарии), tests/unit/test_proposal_lock.cpp (реестр org-локов).
- Docs: docs/01-data-scheme.md §5 (порядок блокировок), docs/04-background-workers.md (инвариант forget), docs/05-mcp-contracts.md (dissolve/join семантика) — синхронизируются в рамках этого изменения.
