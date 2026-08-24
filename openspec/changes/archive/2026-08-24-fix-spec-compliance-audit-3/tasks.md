# Tasks: fix-spec-compliance-audit-3

## 1. Плейбук: STANDARD-предложение разрешено

- [x] 1.1 Заменить §7 PROPOSING в `kPlaybookText` (src/mcp/tools/Playbook.cpp) на каноническую фразу из design D1 («optionally AT MOST ONE of … neither is valid too»); текст docs/14-agent-playbook.md (§ про create_proposal) привести к той же фразе по смыслу. Проверка: сборка зелёная.
- [x] 1.2 Unit-тест на get_playbook: текст содержит явное разрешение предложения без action/config_delta и запрет обоих одновременно; tools/list-каталог не изменил остальные записи. Проверка: тест зелёный.
- [x] 1.3 Явный integration/e2e-ассерт: create_proposal только с org_id+title возвращает ACTIVE без ошибки (усилить существующее покрытие Scenario.h::createProposal проверкой ответа proposal_id/expires_at/status). Проверка: тесты зелёные.

## 2. category при create_organization

- [x] 2.1 Добавить опциональный string-аргумент category в inputSchema create_organization (CreateOrg.cpp) и парсинг: присутствует и не строка → -32602; иначе org.category = значение до putOrg/setCategory (порядок и общий WriteBatch не менять); добавить category в ответ инструмента. Проверка: сборка зелёная.
- [x] 2.2 Integration-тесты (test_discovery_index.cpp или новый кейс): создание с category "Governance" → ответ и get_organization содержат её; search_organizations {category:"governance"} находит сразу после создания; регистронезависимость ("Infra"/"infra"); без category → пустая строка и отсутствие в выдаче фильтра; category массивом → -32602. Проверка: тесты зелёные.

## 3. Ключ аудита без коллизий (design D3)

- [x] 3.1 Перенести seq/salt в экземпляр AuditLogRepository (std::atomic<int64_t> seq_, uint64_t salt_ от std::random_device со смешиванием), убрать g_seq и `% 1000`; salt генерируется в конструкторе. Проверка: сборка зелёная.
- [x] 3.2 Новый формат Keys::auditKey(orgId, tsMs, salt, seq) = `audit:{org|_}:{ts %020lld}:{salt %016llx}{seq %019lld}`; обновить вызов в append. Проверка: сборка зелёная.
- [x] 3.3 Обновить unit test_keys.cpp: фиксированные ширины полей, лексикографический порядок ключей одного бута совпадает с порядком seq, префикс audit:{org}: не зависит от хвоста. Проверка: тест зелёный.
- [x] 3.4 Integration-тест журнала (test_storage_repos.cpp или новый): 1500 событий одной организации с одним created_at → listByOrg возвращает все 1500; второй экземпляр репозитория над той же БД (имитация рестарта) дописывает события в тот же ms → прежние записи неизменны, суммарное число равно числу событий; порядок внутри бута хронологический. Проверка: тесты зелёные.

## 4. RocksDB Statistics в /metrics (design D4)

- [x] 4.1 Включить opts.statistics = rocksdb::CreateDBStatistics() в RocksDBWrapper::open(); добавить метод publishStatisticsToRegistry(), читающий 9 тикеров из таблицы design D4 и кладущий гейджи voterpool_rocksdb_* в MetricsRegistry. Проверка: сборка зелёная.
- [x] 4.2 Добавить 9 записей в kCatalog (Metrics.cpp) с типом gauge, человекочитаемым HELP и без лейблов; вызвать publishStatisticsToRegistry() из обработчика GET /metrics перед expose() (при закрытой БД — пропуск публикации). Проверка: сборка зелёная.
- [x] 4.3 Тесты: test_metrics_exposition.cpp — после открытия БД и записей выдача содержит все семейства voterpool_rocksdb_* c # HELP перед # TYPE, значения неотрицательны, лейблов нет; test_metrics_semantics.cpp — значения block_cache_usage ≥ 0 и wal_synced_total монотонны между двумя скрейпами с записью между ними. Проверка: тесты зелёные.

## 5. Checkpoint — чистый снимок (design D5)

- [x] 5.1 Убрать из runCheckpointCommand (src/storage/Checkpoint.cpp) SchemaManager::run(), гейт kFatalNewerSchema и include SchemaVersion.h; поток: load config → open → отказ при !DbHealth::healthy() → CreateCheckpoint → close(). Проверка: сборка зелёная.
- [x] 5.2 Integration-тест в test_checkpoint_restore.cpp: собрать v1-фикстуру (хелпер по образцу writeRawV1DbWithCorruptRecord без битой записи), выполнить checkpoint → rc=0, версия схемы источника осталась 1, записи cf_proposals не получили config_at_creation; развернуть снимок в новую директорию, открыть Harness'ом → миграция прошла, версия текущая, данные читаемы. Отдельный кейс: база с версией выше бинарника → checkpoint завершается кодом 0 (гейт убран). Проверка: тесты зелёные.

## 6. Документация

- [x] 6.1 docs/05-mcp-contracts.md §1.2: category в аргументах и ответе create_organization (+ примечание о немедленной индексации); docs/14-agent-playbook.md уже правлен в 1.1 — сверить дословность канона. Проверка: grep-сверка фразы плейбука в Playbook.cpp и docs/14.
- [x] 6.2 docs/01-data-scheme.md (cf_audit_log): новый формат ключа audit:{org}:{ms}:{salt}{seq}, гарантия уникальности, порядок внутри бута. Проверка: текст соответствует design D3.
- [x] 6.3 docs/11-observability.md §3 и примечание о Statistics: префикс voterpool_rocksdb_ вместо rocksdb_, состав 9 метрик. Проверка: список совпадает с kCatalog.
- [x] 6.4 docs/13-backup-recovery.md: checkpoint не выполняет миграций и логических мутаций источника (Flush при close меняет только физическую раскладку), снимок возможен при любой версии схемы, миграция — на стороне восстановления. Проверка: текст соответствует design D5.

## 7. Финальная верификация

- [x] 7.1 Прогнать полный набор тестов (unit + integration + e2e по сценарию build.sh/CMake-пресета проекта) — все зелёные, включая обновлённые test_keys/test_metrics_*/test_checkpoint_restore и новые кейсы. Проверка: команда тестов завершается кодом 0.
- [x] 7.2 openspec validate fix-spec-compliance-audit-3 --strict без ошибок. Проверка: валидация проходит.
