## 1. Таргет сборки и каркас

- [x] 1.1 Добавить в tests/CMakeLists.txt таргет `voterpool_bench` (EXCLUDE_FROM_ALL, линковка voterpool_core) для bench/voterpool_bench.cpp; Verify: `cmake --build build --target voterpool_bench` собирает пустой main без предупреждений
- [x] 1.2 Каркас бинарника: разбор аргументов (`<label> <read|write>` + флаги --threads/--duration/--agents/--proposals), RAII-временная директория RocksDB как в hot_path_bench, init AppContext с FixedClock; Verify: запуск без сценария печатает usage в stderr и возвращает код 2
- [x] 1.3 Писатель отчёта: одна JSON-строка в stdout (jsoncpp StreamWriterBuilder) с полями из design D8; Verify: прогон любого сценария выдаёт валидный JSON (проверка парсером python -m json.tool)

## 2. Setup фазы (общий)

- [x] 2.1 Реализовать seed: register_agent ×A, организации (OPEN, MAJORITY, voting_duration_sec с запасом), join всех агентов, create_proposal ×P равномерно по организациям; хронометраж setup_s; Verify: smoke-прогон с A=6 P=3 создаёт 6 агентов/3 предложения (проверка через вывод label/counts или stderr). Уточнение по ходу apply (design D3): организациям сценария write задаётся CONSENT/EQUAL — ранний консенсус MAJORITY закрывал предложение после Y > T/2 и обращал остаток пула в -32003
- [x] 2.2 Построить пул слотов (AgentContext*, proposal_id) = A×P, std::shuffle; Verify: размер пула в JSON равен agents×proposals

## 3. Сценарий read

- [x] 3.1 Рабочие потоки (--threads, дедлайн --duration): итерация = search_organizations → get_proposal → дубликат cast_vote (-32003); учёт ok/ошибок по кодам, длительности операций в per-thread вектора; Verify: прогон --duration 2: все ошибки только -32003, ops > 0
- [x] 3.2 Итог: rps = ops/фактическая длительность, p50_us/p99_us слиянием векторов; Verify: два последовательных прогона дают RPS одного порядка (±30%)

## 4. Сценарий write

- [x] 4.1 Дренаж пула: атомарный fetch_add индекса, cast_vote по слоту до исчерпания или дедлайна; раннее завершение при исчерпании; Verify: однопоточный прогон на малом пуле завершается раньше duration, rps посчитан по фактической фазе
- [x] 4.2 Корроборация WAL: publishStatisticsToRegistry() до/после фазы, wal_synced_delta в JSON; предупреждение в stderr без падения. Уточнение по ходу apply (design D7): групповой коммит WAL считает fsync-события, поэтому при конкурентности delta < ok — норма; строгая проверка delta == ok только при --threads 1. Verify: wal_synced_delta == ok на однопоточном smoke-прогоне (18 == 18), при --threads 4 предупреждение отсутствует
- [x] 4.3 Инварианты: ok + errors == ops, каждый слот использован ровно один раз (ассерты процесса); Verify: инвариант нарушить искусственно невозможно — проверка чтением кода + smoke

## 5. Документы

- [x] 5.1 docs/00-initial-prd.md §5 NFR-1: заменить численную планку на формулировку «максимально высокая достижимая пропускная способность… измеряется бенчмарками (tests/bench/, docs/10)», сохранив пояснения про агрегированную нагрузку, per-proposal mutex и групповой коммит WAL; Verify: grep по docs/00 не находит «50 000»/«50k» в NFR-1, ссылки docs/01 §5 на NFR-1 остаются осмысленными
- [x] 5.2 docs/10-testing.md: §3.3 — упоминание таргета voterpool_bench со сценариями read/write и политикой «не в дефолтном ctest, тяжёлые прогоны — явно и на отдельной машине»; строка карты покрытия (NFR-1) → bench/voterpool_bench.cpp (сценарии); Verify: grep по docs/10 не находит несуществующих имён файлов

## 6. Финальная проверка

- [x] 6.1 Собрать всё: voterpool_bench, hot_path_bench не тронут, полный ctest-набор не содержит бенчмарка; Verify: `ctest --test-dir build -N | grep -c bench` == число seam_isolation-кейсов без bench-таргетов
- [x] 6.2 Smoke-отчёт обоих сценариев (дефолты): read ~10 сек, write ≤ 10 сек; обе JSON-строки содержат полный набор полей D8; Verify: приложить оба однострочника к итогам apply
