# Tasks: Extract ClientInfo Metadata

## 1. Хелперы логирования

- [x] 1.1 Генератор request_id (`req-<pid:x>-<counter:x>`, atomic counter) + unit-тест уникальности и формата. Проверка: тест зелёный.
- [x] 1.2 `sanitizeLogValue`: обрезка 64 символа, удаление управляющих символов (<0x20, 0x7f); unit-тесты на пустую строку, кириллицу, `\n`, длинную строку. Проверка: тесты зелёные.

## 2. Обработчик /mcp

- [x] 2.1 Извлечь `params._meta["io.modelcontextprotocol/clientInfo"]` {name, version} из распарсенного root; инкремент `voterpool_mcp_client_meta_total{present}`. Проверка: ручной curl с/без clientInfo меняет семплы.
- [x] 2.2 Расширить recordRequestMetrics контекстом (durationMs, agentId, clientInfo, errorCode) и выпустить структурную строку D1 на всех путях handler'а (parse error, invalid request, unauthorized, discover, tools/list, tools/call ok/error, direct ok/error). Проверка: лог-строка присутствует для каждого пути (ручной прогон или e2e-лог-захват).
- [x] 2.3 Токены не попадают в лог: unit-тест форматтера на входе с bearer-подобными строками в аргументах — их нет в выводе; clientInfo присутствует санитизированным. Проверка: тест зелёный.

## 3. Middleware

- [x] 3.1 Лог-строка на rejectProtocol и unauthorized (request_id, outcome=error, error_code, причина; без тела и токенов). Проверка: e2e отклонённого запроса не ломается; лог виден при ручном прогоне.

## 4. Тесты

- [x] 4.1 Unit: форматтер покрывает все поля D1, прочерки отсутствующих значений, error_code только при error. Проверка: тест зелёный.
- [x] 4.2 E2E: вызов tools/list с clientInfo и без → `/metrics` содержит растущие семплы voterpool_mcp_client_meta_total{present="true"} и {present="false"}; лейблов с именем клиента нет. Проверка: тест зелёный.
- [x] 4.3 Полный регресс unit+integration+e2e; доработать упавшие тесты, если изменения формата ответов их задели (не должны). Проверка: ctest полностью зелёный.

## 5. Документация

- [x] 5.1 docs/11: строка voterpool_mcp_client_meta_total в таблицу §3 + пример структурной записи лога в §2 (поля из D1). Проверка: пример соответствует фактическому формату строки из кода.
