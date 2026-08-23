# Tasks: autonomous-onboarding-flow

## 1. Протокол: `_meta`-канал авторизации

- [x] 1.1 Добавить в McpHandler резолв `params._meta["io.voterpool/auth"]["bearer"]` при отсутствии agent_context в атрибутах запроса: валидация через IAuthProvider, построение AgentContext; невалидный токен → -32001. Проверить unit-тестами матрицу (валидный/невалидный/нет × заголовок/_meta/оба) — сценарии specs/mcp-protocol «Резервный канал авторизации через _meta»
- [x] 1.2 Убедиться, что bearer-значения не попадают в логи и метрики ни одним путём (инспекция spdlog-вызовов + тест на отсутствие токена в логах при -32001 через _meta); зафиксировать правило в docs/11
- [x] 1.3 e2e-тест: полный защищённый вызов (get_agent → create_organization) без заголовка Authorization, только _meta; приоритет заголовка при обоих каналах

## 2. Инструмент get_playbook

- [x] 2.1 Ввести флаг `anonymous` в ToolDef и заменить хардкод register_agent в dispatchTool; убедиться, что server/discover и tools/list не изменились. Проверить юнит-тестом каталога
- [x] 2.2 Создать src/mcp/tools/Playbook.cpp с get_playbook (anonymous=true, без обязательных аргументов): компактная статическая версия плейбука со ссылкой на docs/14; description с приглашением вызвать первым. Проверить появление в tools/list лексикографически (юнит-тест)
- [x] 2.3 e2e-тест: анонимный вызов get_playbook без Authorization и _meta возвращает плейбук; сценарий specs/mcp-protocol «Анонимный вызов плейбука»

## 3. Инструмент get_proposal

- [x] 3.1 Добавить VoteRepository::listByProposal (скан префикса Keys::vote по org+proposal); юнит-тест на порядок и полноту {agent_id, decision, power_at_vote}
- [x] 3.2 Создать src/mcp/tools/GetProposal.cpp: аргумент {proposal_id}, резолв org_id через proposal_lookup, проверка ACTIVE-членства (-32002/-32004), полная карточка + votes для участников. Юнит-тесты сценариев specs/mcp-protocol «Инструмент get_proposal»

## 4. Фильтр updated_since у get_proposals

- [x] 4.1 Расширить GetProposals.cpp: опциональный integer updated_since >= 0 (-32602 при неверном типе), пост-фильтр updated_at > since, поле updated_at в каждом элементе ответа. Юнит-тесты: инкрементальный опрос, обновление при голосе и закрытии (сценарии specs/mcp-protocol «Фильтр updated_since»)

## 5. Видимость PENDING-заявок

- [x] 5.1 JoinOrg.cpp: эмиссия SseEvent join_requested {org_id, agent_id, requested_at=membership.created_at} строго при создании новой PENDING-записи (идемпотентный повтор — без события). Integration-тест на очередь событий
- [x] 5.2 Создать src/mcp/tools/ListPendingMembers.cpp: скан Keys::pendingPrefix(orgId), чтение Membership.created_at, сортировка по requested_at asc, права requireActiveMember (-32002/-32004). Юнит-тесты сценариев specs/organizations «Видимость PENDING-заявок», включая исчезновение кандидата после PASSED APPROVE_MEMBER
- [x] 5.3 Integration/e2e: подписанный ACTIVE участник получает SSE join_requested при заявке в CLOSED; OPEN-вступление генерирует только member_joined (сценарии specs/sse-events «Событие join_requested о новых заявках»)

## 6. Документация docs/

- [x] 6.1 Новый docs/14-agent-playbook.md (русский): полный гайд агента — подключение, регистрация и атомарное сохранение пары в конфиге MCP server харнесса, update_agent, discovery, вступление OPEN/CLOSED (холодный старт создателя внутри примера, без акцента), рабочий цикл, контракт исходов (синхронный cast_vote / expires_at+δ / эффекты / SSE-ускоритель), poll-until-ACTIVE, мульти-орг этикет, правила безопасности ключа
- [x] 6.2 docs/05-mcp-contracts.md: §1.0 — _meta-канал (формат, приоритет, режим A only, запрет логирования); новые контракты §1.18 get_playbook, §1.19 get_proposal, §1.20 list_pending_members; updated_since в §1.6; перечень анонимных методов
- [x] 6.3 docs/06-server-sent-events.md: событие join_requested в каталог с payload и триггерами
- [x] 6.4 docs/03-authorization-and-isolation.md: §1.1.2 — хранение пары в конфигурации MCP server харнесса как основной способ (файл — fallback); контракт poll-until-ACTIVE для PENDING-кандидатов
- [x] 6.5 docs/00-initial-prd.md: §4 — дополнить список MCP Tools инструментами get_playbook, get_proposal, list_pending_members и параметром updated_since; §2.4 — поле updated_at у Proposal; новый FR о видимости PENDING-заявок и _meta-канале авторизации
- [x] 6.6 docs/09-project-structure.md: отразить новые файлы mcp/tools (Playbook.cpp, GetProposal.cpp, ListPendingMembers.cpp)
- [x] 6.7 docs/10-testing.md: описать новые зоны покрытия — матрица _meta-авторизации, updated_since-семантика, join_requested, list_pending_members, автономный онбординг e2e
- [x] 6.8 Согласованность перекрёстных ссылок docs (00↔03↔05↔06↔10↔11↔14): grep на упоминания новых инструментов, событий и параметров; устранить расхождения нумерации секций

## 7. README (зеркально RU/EN)

- [x] 7.1 Раздел Provisioning в README.md: curl register_agent, готовый пример mcp-config (url + Authorization Bearer; AGENT_ID опциональной env ${VOTERPOOL_AGENT_ID}), предупреждение о логировании тел прокси; зеркально в README.ru.md
- [x] 7.2 Раздел Architecture Diagrams c Mermaid: sequenceDiagram онбординга, stateDiagram членства, stateDiagram предложения (черновики из обсуждения); зеркально в README.ru.md
- [x] 7.3 Ссылки на docs/14-agent-playbook.md из обоих README рядом с Quick Start; проверка рендера Mermaid-блоков

## 8. Корректировка существующих тестов

Перед правками зафиксировать текущие падения: прогон полного набора после выполнения разделов 1–5.

- [x] 8.1 Прогон ctest --test-dir build --output-on-failure ДО правок, список упавших тестов в отчёт задачи; обновить тесты каталога tools/list (число инструментов, лексикографический порядок, схемы новых тулз) под +3 инструмента
- [x] 8.2 Обновить тесты анонимных методов и AuthMiddleware/McpHandler: множество методов без авторизации пополняется get_playbook; матрица каналов авторизации (заголовок/_meta/оба/нет)
- [x] 8.3 Обновить ассерты формы ответа get_proposals (новое поле updated_at в каждом элементе) и inputSchema (опциональный updated_since)
- [x] 8.4 Обновить e2e SSE-тесты последовательностей событий: join_organization в CLOSED теперь добавляет join_requested до member_joined; тесты точных потоков событий скорректировать
- [x] 8.5 Обновить e2e протокольные тесты контракта ошибок/каталога, если они фиксируют полный набор имён методов; убедиться, что режим B для новых тулз идентичен режиму A

## 9. Тестирование нового функционала (системный уровень)

- [x] 9.1 Integration (реальный RocksDB, MockClock): полный жизненный цикл CLOSED-вступления — join → PENDING → join_requested → list_pending_members (сортировка по requested_at) → ACTION APPROVE_MEMBER → PASSED → кандидат ACTIVE → исчез из списка; повторный join идемпотентен и не эмитит событие
- [x] 9.2 Integration: TTL-воркер закрывает истекшее предложение и обновляет updated_at временем закрытия в той же транзакции; последующий get_proposals {updated_since: T} возвращает закрытое предложение; досрочное закрытие cast_vote тоже сдвигает updated_at
- [x] 9.3 e2e: автономный онбординг end-to-end клиентом БЕЗ статических заголовков (_meta-only): register_agent → get_playbook → update_agent → search_organizations → join CLOSED → второй агент (header-клиент) видит заявку и одобряет консенсусом → кандидат discover-ит активацию poll-until-ACTIVE → create_proposal → cast_vote вторым → обнаружение исхода через get_proposals(updated_since) и get_proposal (агрегаты + голоса)
- [x] 9.4 e2e изоляция и ошибки: не-участник не получает join_requested по SSE и получает -32002 на list_pending_members/get_proposal; невалидный _meta-токен → -32001; SSE без заголовка отклоняется даже с _meta в теле GET
- [ ] 9.5 Конкурентность: ctest --preset tsan на затронутых путях (эмиссия события из JoinOrg в lock-free очередь, параллельные cast_vote и updated_since-сканы); гонок нет. НЕ ВЫПОЛНЕНО — блокер среды: TSAN-рантайм на этой машине (aarch64, GCC 12) сегфолтится при старте любого TSAN-бинарника (tsan_platform_linux.cpp:315, смена personality запрещена контейнером); выполнить на машине с рабочим TSAN

## 10. Верификация

- [x] 10.1 Полный зелёный прогон: ctest --test-dir build --output-on-failure (unit + integration + e2e)
- [x] 10.2 openspec validate autonomous-onboarding-flow --strict без ошибок
