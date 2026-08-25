## 1. Порты и локальные имплементации

- [x] 1.1 Создать `include/scaling/{IDirectory,IIdentity,IEventBus}.h` по составу design D2 (чистые интерфейсы, виртуальные деструкторы) с doxygen-комментариями контрактов. Verify: заголовки компилируются standalone, `clang-tidy`/сборка без предупреждений -Wall -Wextra.
- [x] 1.2 Реализовать `src/scaling/local/{LocalDirectory,LocalIdentity,LocalEventBus}.cpp`: обёртки над OrgNameRegistry/IndexRepository/AgentRepository/SseHub с сохранением текущей семантики; LocalDirectory::resolveProposal читает `proposal_lookup:`. Verify: модульные тесты 1.3 зелёные.
- [x] 1.3 Контрактные тесты портов (параметризованный fixture: Local* имплы + фейки): matchQuery/scanTag/scanCategory/scanFeedActive/resolveProposal; resolveToken ok/неизвестный/пустой; createAgent уникальность и повтор; listOrgsOfAgent для агента без оргов → пусто; recordMembershipLink/removeMembershipLink симметрия; подписка/отписка EventBus. Edge: конкурентные вызовы IIdentity под tsan. Verify: новые тесты зелёные, tsan-прогон чист.

## 2. Перевод call-site'ов

- [x] 2.1 AppContext: создать Local*-имплы после репозиториев, expose через ToolContext; парсинг `cluster.mode` в Config (допустимо standalone/отсутствие, иное → fail-fast exit 1 до открытия порта). Verify: юнит-тест Config на три значения mode; старт приложения со старым config/default.yaml успешен.
- [x] 2.2 AuthMiddleware → IIdentity::resolveToken; RegisterAgent → IIdentity::createAgent. Verify: существующие интеграционные тесты auth/register зелёные.
- [x] 2.3 SearchOrganizations → IDirectory (matchName/scanTag/scanCategory/scanFeedActive); GetProposals/cast_vote резолв proposal→org → IDirectory::resolveProposal. Verify: интеграционные тесты search и cast_vote зелёные.
- [x] 2.4 AgentProfile → IIdentity::{getProfile,listOrgsOfAgent}; JoinOrg/LeaveOrg → IIdentity::record/removeMembershipLink внутри существующего WriteBatch (атомарность D3). Verify: тесты agent_profile/join/leave зелёные; edge «агент без организаций» покрыт.
- [x] 2.5 SseHub: точки подписки all-orgs через IEventBus (sink-абстракция), доставка и порядок внутри org не меняются. Verify: SSE integration-тесты зелёные.
- [x] 2.6 Статическая проверка изоляции швов: скрипт grep'ает файлы вне src/scaling на прямые обращения к OrgNameRegistry и auth-методам AgentRepository; подключить в CMake test target. Verify: скрипт падает при намеренной вставке прямого вызова, зелёный на чистом дереве.

## 3. Golden-регрессия и производительность

- [x] 3.1 Зафиксировать эталон: прогнать канонический сценарий (register→create_org→join→create_proposal→cast_vote→закрытие→search_organizations→get_agent→SSE-подписка/событие) на pre-change бинарнике; ответы сохранить как fixture JSON (tools/list snapshot включительно). Verify: fixture закоммичен, воспроизводим командой из README теста.
- [x] 3.2 Golden-тест post-change: тот же сценарий на новом коде, глубокое сравнение структур/кодов ошибок с fixture. Verify: diff нулевой; при легитимном расхождении — процедура обновления fixture отдельным коммитом с ревью.
- [x] 3.3 Бенчмарк p99 cast_vote и search_organizations до/после (одинаковая нагрузка). Verify: регрессия <2% p99; иначе devirtualize по design Risks и перемерить.

## 4. Документация

- [x] 4.1 docs/09 §1.1: добавить `include/scaling/`, `src/scaling/` с комментариями портов; docs/00 NFR-3 — ссылка на порты Этапа 1. Verify: дерево соответствует фактическим файлам.
- [x] 4.2 docs/16 §5: пометить Этап 1 выполненным (после мержа); конфигурация cluster.mode задокументирована в docs/08 (секция cluster, допустимое значение standalone). Verify: упоминания соответствуют реализованному поведению fail-fast.

## 5. Финальная верификация

- [x] 5.1 Полный набор: cmake build Release + ctest (все существующие + новые) зелёные, включая golden-регрессию 3.2. Verify: ctest exit 0; сводка приложена (notes/test-summary.md: 198/198). TSan-прогон — ПРИНЯТ С ОГОВОРКОЙ: среда разработки запрещает syscall personality(ADDR_NO_RANDOMIZE), TSan падает на старте любого бинарника; готовая команда для хоста зафиксирована в notes/test-summary.md.
