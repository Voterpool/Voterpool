# Tasks — fix-spec-compliance-audit-2

## 1. Миграция и хранилище (дефект 7)

- [x] 1.1 `SchemaVersion.cpp`: разделить валидный-старый-JSON (мигрировать) и не-JSON (critical-лог с ключом, return false без записей в батч и без writeVersion); двухпроходная схема validate→mutate по design D7. Проверка: integration-тест из 1.2 зелёный.
- [x] 1.2 Тесты `tests/integration/test_migration.cpp`: (a) битая запись → миграция не проходит, версия остаётся 1, соседние записи не изменены; (b) рестарт после удаления битой записи → миграция дозавершается, ранее преобразованные записи без повторных изменений; (c) старый формат с extra-полями сохраняет их. Прогон: ctest -R migration.
- [x] 1.3 `AppContext.cpp`: `SchemaGateResult::kError` → throw (фатальный старт, exit 1 через VoterpoolApp::init). Проверка: тест 1.2a фиксирует ненулевой код выхода/исключение инициализации.

## 2. Наблюдаемость (дефект 6)

- [x] 2.1 `Metrics.cpp`: HELP-тексты из docs/11 §3 в registerDefaults/observe; expose() печатает `# HELP` перед `# TYPE` всегда. Проверка: unit-тест формата экспозиции (2.2).
- [x] 2.2 Тест `tests/unit/test_metrics_exposition.cpp` (новый): после registerDefaults expose() содержит для каждого семейства HELP с текстом ≠ имени до TYPE; сценарий «таймерное EXPIRED не растит early_exit» на уровне движка — в 2.3.
- [x] 2.3 `ConsensusEngine.cpp`: перенести incCounter early_exit из finalizeLocked в vote-path при REJECTED. Тесты: integration — таймерное закрытие EXPIRED не увеличивает счётчик, vote-path REJECTED (MAJORITY невозможность PASSED / CONSENT возражение) увеличивает ровно на 1. Прогон: ctest -R "consensus|ttl".
- [x] 2.4 `AgentRepository.cpp` + `AppContext.cpp` + `Metrics.cpp`: agents_total → gauge (убрать incCounter, setGauge(count()) в create и при инициализации контекста). Тест: unit/integration — рестарт harness на базе с N агентами → gauge == N без регистрации новых; create → N+1. Прогон: ctest -R storage.

## 3. MCP-протокол (дефекты 4 и bonus)

- [x] 3.1 `ToolRegistry.{h,cpp}`: хелперы схем (string/number/integer/boolean/array/enum) + schemaObject со свойствами-объектами. Проверка: компиляция каталога, unit-тест структуры.
- [x] 3.2 Все tool-дефы `src/mcp/tools/*.cpp`: перевести на дескрипторы; cast_vote.decision — enum [YES, NO, ABSTAIN]. Проверка: сборка без предупреждений.
- [x] 3.3 Усилить `tests/e2e/test_protocol.cpp`: каждый inputSchema — object; properties[x] — объект с type ∈ {string,number,integer,boolean,array}; required ⊆ properties; decision enum присутствует. Прогон: ctest -R protocol.
- [x] 3.4 `AuthMiddleware.cpp`: неразборчивое тело (probe.method пуст) проходит к хендлеру независимо от Mcp-Method/Mcp-Name; проверки версии только для разобранных тел. Тест e2e: битый JSON + `Mcp-Method: server/discover` → HTTP 400, code -32700, id null. Прогон: ctest -R errors_http.

## 4. Сервер и транспорт (дефекты 2, 5, 8)

- [x] 4.1 `Config.cpp`/`VoterpoolApp.cpp`: setSSLFiles + addListener(useSSL) при ssl.enabled=true; pre-check cert/key в validate() (exit 1); setClientMaxBodySize(max_request_body_size); setIdleConnectionTimeout(request_timeout_sec). Проверка: unit-тест validate() на отсутствующий cert_path; e2e-подъём с self-signed сертификатом (генерация в tmpdir) → HTTPS /health 200.
- [x] 4.2 Тест e2e transport: тело больше max_request_body_size отклоняется без диспетчеризации (инструментальные метрики не растут); plaintext /health работает при ssl.enabled=false.
- [x] 4.3 `VoterpoolApp.cpp`: добавить заголовок `Connection: keep-alive` в ответ /mcp/events. Тест e2e sse: все четыре заголовка присутствуют. Прогон: ctest -R sse_events.
- [x] 4.4 Draining-флаг (design D8): CAS-идемпотентность в drainAndQuit; pre-handling advice → HTTP 503 для POST /mcp после сигнала; /health и /metrics доступны. Тест e2e shutdown: SIGTERM при активной подписке → server_shutdown получен, POST во время drain → 503 мгновенно, exit code 0, повторный SIGTERM безопасен. Прогон: ctest -R shutdown.

## 5. Конфигурация (дефект 3)

- [x] 5.1 `Config.cpp` applyEnv: полная таблица ключей по design D3. Тест `tests/unit/test_config.cpp`: параметризованный прогон — каждый VOTERPOOL_* ключ меняет соответствующее поле AppConfig; приоритет env над yaml сохранён. Прогон: ctest -R config.

## 6. Организации (дефект 1)

- [x] 6.1 `OrgGovernance.cpp`: leave_organization для не-участника → notFound(-32004), единый вид ответа с DISSOLVED-веткой. Тест integration: не-участник → -32004; DISSOLVED → -32004 (регресс); участник → LEFT. Прогон: ctest -R onboarding|mcp_flow.

## 7. Docs и финальный прогон

- [x] 7.1 docs/08-app-config.md: снять пометку «(Enterprise)» с секции ssl, описать применение TLS/max_request_body_size/request_timeout_sec в On-Premises. README: пример включения TLS. Проверка: grep не находит «ssl ... Enterprise».
- [x] 7.2 Полный прогон набора: cmake build + ctest (unit, integration, e2e) — все зелёные; openspec validate --strict для change.
