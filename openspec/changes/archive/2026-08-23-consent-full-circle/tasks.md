## 1. Ядро модели и домен

- [x] 1.1 `include/domain/Proposal.h`: добавить `std::int64_t eligible_voters_at_creation = 0;`; проверить сборку (`cmake --build build -j`)
- [x] 1.2 `include/consensus/IConsensusModel.h` (ConsentModel::evaluate): новый порядок правил — REJECTED при `no_power > kEps`; PASSED при `yes_power > kEps && voters_count >= eligible_voters_at_creation`; EXPIRED при timeExpired; иначе ACTIVE. Проверка: обновлённые unit-тесты ConsentModel зелёные
- [x] 1.3 `include/storage/Codec.h`: сериализация/десериализация `eligible_voters_at_creation` (дефолт декода 0). Проверка: roundtrip в тестах репозиториев

## 2. Создание предложения и валидация конфигурации

- [x] 2.1 `src/mcp/tools/CreateProposal.cpp`: фиксировать `p.eligible_voters_at_creation = orgs->countActiveMembers(org_id)` до коммита батча. Проверка: get_proposal возвращает H = числу ACTIVE участников
- [x] 2.2 `include/mcp/tools/ToolHelpers.h` (parseOrgConfig): отклонять CONSENT+SHARES ошибкой -32005 (покрывает create_organization и config_delta). Проверка: интеграционный тест на оба пути
- [x] 2.3 `src/mcp/tools/GetProposals.cpp` и `src/mcp/tools/GetProposal.cpp`: добавить поле `eligible_voters_at_creation` в ответы. Проверка: существующие e2e не падают, новое поле присутствует

## 3. Тесты

- [x] 3.1 Переписать CONSENT-блок `tests/unit/test_consensus_models.cpp`: первый YES держит ACTIVE; полный круг с ABSTAIN → PASSED; любой NO → немедленный REJECTED; таймаут частичной явки с YES → EXPIRED; все воздержались → EXPIRED; одиночный круг → PASSED; legacy H=0 ведёт себя по-старому
- [x] 3.2 Новый интеграционный тест (например, `tests/integration/test_consent_flow.cpp`): 3 участника EQUAL+CONSENT — круг из YES+ABSTAIN закрывается PASSED; NO закрывает REJECTED до истечения; TTL при неполном круге даёт EXPIRED; CONSENT+SHARES отклонён на create_organization и config_delta (-32005); новичок, вступивший во время голосования, участвует в voters_count. Зарегистрировать файл в tests/CMakeLists.txt, если требуется
- [x] 3.3 Проверить затронутые сценарии: `test_ttl_worker` (ABSTAIN-only → EXPIRED остаётся), `test_actions` (CONSENT-орг из одного участника), e2e `test_sse_events`, `test_onboarding_http`, `test_checkpoint_restore`

## 4. Документация и спеки

- [x] 4.1 docs/02-consensus-math.md: §1.4.3 новая формула (H, полный круг, EXPIRED при неполном круге); §1.1.1 строка CONSENT; §1.2.2 ограничение «CONSENT только с EQUAL»; §1.5 краевые случаи (новички и C ≥ H; H = 0 legacy; убрать zero-power кейс для CONSENT)
- [x] 4.2 docs/01-data-scheme.md: поле Proposal `eligible_voters_at_creation`, уточнить комментарий voters_count
- [x] 4.3 docs/00-initial-prd.md (формула CONSENT) и docs/10-testing.md:26 (новая матрица исходов + валидация CONSENT⇒EQUAL)
- [x] 4.4 docs/05-mcp-contracts.md: контракты create_organization/create_proposal/config_delta — ограничение CONSENT⇒EQUAL и ошибка -32005; состав ответов get_proposals/get_proposal
- [x] 4.5 Основные спеки после принятия дельт: openspec/specs/consensus-engine/spec.md (Requirement CONSENT, Создание предложения, Замороженные T и H) и openspec/specs/organizations/spec.md (Создание организации)

## 5. Верификация

- [x] 5.1 Полная сборка: `cmake --build build -j`
- [x] 5.2 Полный прогон: `ctest --test-dir build --output-on-failure` — все тесты зелёные; починить регрессии, если выявлены
