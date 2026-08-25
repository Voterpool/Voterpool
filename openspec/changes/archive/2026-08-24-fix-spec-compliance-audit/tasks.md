## 1. Дефект #5 — crash при rebuild_index_on_start (блокирует всё тестирование запуска)

- [x] 1.1 В `AppContext::init` перенести конструирование репозиториев (agents/orgs/proposals/votes/indexes/audit) до блока `rebuild_index_on_start`; убедиться, что блок использует уже сконструированный `proposals`. Проверка: интеграционный тест (шаг 6.1) проходит без SIGSEGV.

## 2. Дефект #1 — PENDING-членства в обратном индексе

- [x] 2.1 В `OrgRepository::putMembership(batch, m)` писать `cf_agent_orgs` безусловно (при любом MemberStatus); значение ключа оставить `role/status`. Проверка: новый тест в tests/integration/test_storage_repos.cpp — PENDING виден в `listOrgsOfAgent`.
- [x] 2.2 Integration-тест пограничных переходов: PENDING→ACTIVE не создаёт дубль ключа; deleteMembership удаляет индексную запись; повторный put идемпотентен. Файл: tests/integration/test_storage_repos.cpp.
- [x] 2.3 Integration/e2e-тест сценария спеки agent-identity «Профиль с членствами»: CLOSED-орг + join → get_agent содержит и ACTIVE, и PENDING записи с role/status/voting_power. Файл: tests/integration/test_onboarding_flow.cpp.

## 3. Дефект #2 — резидентный реестр имён (full in-memory scan)

- [x] 3.1 Создать компонент `OrgNameRegistry` (include/storage/OrgNameRegistry.h, src/storage/OrgNameRegistry.cpp): vector<Entry{org_id, name_lower}> + unordered_map<org_id, index>, std::shared_mutex; API matchQuery / findActiveByName / add / rename / erase / load(db). Добавить в CMake. Проверка: unit-тесты на matchQuery (подстрока в начале/середине/конце, пустой query, регистр), findActiveByName, rename/erase.
- [x] 3.2 Подключить в AppContext::init: load() из cf_organizations (только ACTIVE) после открытия БД до подъёма HTTP; передать реестр в ConsensusEngine::Deps и ToolContext/AppContext. Проверка: сборка зелёная, существующие тесты не падают.
- [x] 3.3 CreateOrg: проверку дубликата перевести на `findActiveByName`; после успешного коммита вызвать `add`. Удалить RocksDB-скан `org_name:` из CreateOrg.cpp. Проверка: integration-тест дубликата имени (-32003, любой регистр) из шага 3.7.
- [x] 3.4 SearchOrganizations: заменить `scanNameQuery(query, 5000)` на `matchQuery(queryLowered)` без ограничения числа кандидатов. Проверка: integration-тесты шага 3.5–3.7 зелёные.
- [x] 3.5 Integration-тесты поиска по подстроке (test_discovery_index.cpp): канонический кейс спеки query="council" находит только "AI Council" (при наличии "Dev Guild"); совпадение в середине/конце длинного названия; регистронезависимость ("AI COUNCIL").
- [x] 3.6 Integration-тесты согласованности реестра: после dissolve организация исчезает из name-поиска; после PASSED UPDATE_ORG_INFO поиск находит новое название и не находит старое; рестарт процесса (повторный load) сохраняет результаты поиска.
- [x] 3.7 Удалить мёртвый код: IndexRepository::setName/removeName/scanNameQuery и вызовы в ConsensusEngine (UPDATE_ORG_INFO, dissolveOrganization); grep-аудит остальных потребителей `org_name:`. Обновить затронутые фикстуры тестов (checkpoint/migration), если они считают org_name-ключи. Проверка: grep "org_name" не находит чтений; полный прогон integration зелёный.

## 4. Дефект #3 — config_delta merge-then-validate

- [x] 4.1 Расширить `parseOrgConfig(cfgJson, required, const OrgConfig* base = nullptr)` в ToolHelpers.h: отсутствующие поля наследуются от base (isMember-проверки), валидации — по смерженному итогу. CreateOrg вызывает с base=nullptr. Проверка: unit-тесты parseOrgConfig в test_params_validation.cpp — частичное наследование каждого из 4 полей; CONSENT+SHARES через мерж → ошибка; duration<=0/quorum>100 через мерж → ошибка; невалидная строка enum → invalidParams.
- [x] 4.2 CreateProposal: передавать `&orgOpt->config`, сохранять смерженный полный конфиг в p.config_delta. Проверка: unit-тесты шага 4.1 + integration шага 4.3.
- [x] 4.3 Integration-тесты конфигурации (test_consent_flow.cpp или новый test_config_delta.cpp): частичная {voting_duration_sec:7200} в CONSENT-орге после PASSED даёт CONSENT/EQUAL/51/7200 (get_organization); частичная {power_distribution:"SHARES"} в CONSENT-орге → -32005, предложение не создано; полная дельта CONSENT+SHARES → -32005; get_proposal после применения возвращает полный смерженный config_delta.
- [x] 4.4 Integration-тест: активное предложение продолжает голосовать по старым правилам после смены конфига (регресс консенсуса). Файл: test_consent_flow.cpp.

## 5. Дефект #4 — хранение исхода применения эффектов

- [x] 5.1 Domain: добавить в Proposal поля `bool config_delta_applied = false; bool action_applied = false;`; Codec serializeProposal пишет оба всегда, deserializeProposal читает (отсутствие → false, без деривации). Проверка: unit-тест roundtrip Codec в test_storage_repos.cpp или отдельном test_codec.cpp: true→true, absent→false.
- [x] 5.2 finalizeLocked: перенести первичный put(batch,p) в конец функции; ветки эффектов выставляют p.config_delta_applied / p.action_applied и те же значения в ClosedInfo; ранние выходы оставляют false. Перенести incCounter voterpool_actions_applied_total в точки фактического применения (после limitsOk для APPROVE_MEMBER; в успех UPDATE_ORG_INFO). Проверка: сборка; существующие тесты cast_vote_tx/ttl_worker зелёные.
- [x] 5.3 GetProposal: читать сохранённые флаги (action_applied = kind либо null; config_delta_applied из поля). Проверка: integration-тесты шагов 5.4–5.5.
- [x] 5.4 Integration-тест лимитного кейса спеки consensus-engine: CLOSED-орг max_agents=1, PENDING-агент, PASSED APPROVE_MEMBER → участник остался PENDING; get_proposal action_applied=null и config_delta_applied=false; proposal_closed содержит action_applied=null (согласованность карточки и события); метрика voterpool_actions_applied_total НЕ увеличена. Файл: test_actions.cpp.
- [x] 5.5 Integration-тесты позитивных и пограничных исходов: успешный APPROVE_MEMBER → action_applied="APPROVE_MEMBER", метрика +1; повторный PASSED APPROVE_MEMBER по уже ACTIVE агенту (идемпотентный пропуск) → action_applied=null, метрика не растёт; успешный UPDATE_ORG_INFO → action_applied="UPDATE_ORG_INFO"; PASSED config_delta при нормальном применении → config_delta_applied=true. Файл: test_actions.cpp.

## 6. Полная верификация

- [x] 6.1 Новый integration-тест восстановления: засеянная БД с активным предложением + storage.rebuild_index_on_start=true → init() выживает, лог restored>=1, TTL-воркер закрывает предложение по восстановленному индексу. Файл: test_recovery.cpp.
- [x] 6.2 Прогон полного набора: cmake build + ctest (unit, integration, e2e, e2e.shutdown) — все зелёные. Команда: сборка по CMakePresets, затем ctest --output-on-failure.
- [x] 6.3 Синхронизировать документацию: README/README.ru (раздел поиска — резидентный реестр, точность подстроки), docs/observability (семантика voterpool_actions_applied_total), docs/ по архитектуре хранения (org_name-индекс удалён). Артефакт: обновлённые файлы docs, упомянутые в архивации change.
