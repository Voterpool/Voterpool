## ADDED Requirements

### Requirement: Описания параметров во входных схемах

Каждое свойство inputSchema каждого инструмента в tools/list ДОЛЖНО иметь поле `description` на английском языке, объясняющее назначение, формат и ограничения значения, а инструменты со сложной структурой аргументов (create_organization, create_proposal, wait_proposal_close) ДОЛЖНЫ включать ключ `examples` в описание инструмента с минимум одним корректным полным вызовом. Схемы НЕ ДОЛЖНЫ объявлять обязательными поля, имеющие серверные значения по умолчанию.

#### Scenario: Обнаружимость структуры action без проб и ошибок

- **WHEN** агент читает tools/list и рассматривает inputSchema инструмента create_proposal
- **THEN** свойство action имеет типизированную схему: kind с enum [APPROVE_MEMBER, UPDATE_ORG_INFO] и payload со свойствами target_agent_id (для APPROVE_MEMBER) либо допустимыми полями организации (для UPDATE_ORG_INFO), каждое с description

#### Scenario: Примеры вызовов в каталоге

- **WHEN** агент вызывает tools/list
- **THEN** описания create_organization и create_proposal содержат examples с корректным JSON полного вызова (включая вложенный config для create_organization)

### Requirement: Строгий контракт аргументов инструментов

Диспетчер инструментов ОБЯЗАН отклонять -32602 Invalid params вместо тихого игнорирования: (а) неизвестное имя аргумента верхнего уровня — если оно опечатка известного, data.hint ДОЛЖЕН содержать предположение «did you mean X»; (б) аргумент неверного типа (строка вместо массива/числа/объекта); (в) отсутствие обязательного аргумента — message ДОЛЖЕН называть недостающее поле. Ошибки валидации ДОЛЖны содержать data с именем проблемного поля; существующие допустимые вызовы ОБЯЗАНЫ продолжать проходить валидацию без изменений.

#### Scenario: Опечатка в имени аргумента

- **WHEN** агент вызывает cast_vote {proposal_id: "<uuid>", decison: "YES"}
- **THEN** сервер возвращает -32602 с указанием неизвестного поля decison и hint "did you mean decision?"

#### Scenario: Неверный тип известного поля

- **WHEN** агент вызывает create_organization {name: "N", type: "OPEN", config: {...}, tags: "governance"} (теги строкой)
- **THEN** сервер возвращает -32602 с именем поля tags и ожидаемым типом array of string вместо молчаливого создания организации без тегов

#### Scenario: Отсутствие обязательного поля

- **WHEN** агент вызывает cast_vote {proposal_id: "<uuid>"} без decision
- **THEN** сервер возвращает -32602 Missing required argument: decision

### Requirement: Подсказки в бизнес-ошибках

Ошибки -32002 Forbidden, -32003 Conflict и -32005 Business Rule Violation ДОЛЖНЫ включать машиночитаемый контекст в error.data: для -32002 при отсутствии членства — agent_id вызывающего и org_id; для -32003 при повторном голосовании — previous_decision; для -32005 при недопустимом решении голосования — consensus_model и массив allowed допустимых решений. Поле allowed ДОЛЖНО отражать allowedDecisions модели консенсуса предложения.

#### Scenario: Недопустимое решение для модели консенсуса

- **WHEN** агент голосует ABSTAIN в организации с моделью MAJORITY
- **THEN** ответ содержит -32005 c data.consensus_model = "MAJORITY" и data.allowed = ["YES","NO"]

#### Scenario: Повторное голосование сообщает прежнее решение

- **WHEN** агент повторно вызывает cast_vote по тому же предложению
- **THEN** ответ содержит -32003 с data.previous_decision его первого голоса

### Requirement: Long-poll инструмент wait_proposal_close

Каталог ДОЛЖЕН содержать инструмент `wait_proposal_close`, принимающий {proposal_id, timeout_sec?} (timeout_sec — целое в диапазоне [1; 90], значение по умолчанию задаётся конфигурацией). Инструмент требует ACTIVE-членства в организации предложения (та же видимость, что у get_proposal) и ОБЯЗАН вернуть управление немедленно, если предложение уже терминально; иначе он блокируется до фиксации терминального статуса (PASSED/REJECTED/EXPIRED любым путём: досрочный консенсус, TTL-воркер, роспуск) или до истечения таймаута, после чего возвращает текущий статус и флаг closed=false. Успешный ответ ДОЛЖЕН содержать proposal_id, status, closed (boolean), агрегаты yes_power/no_power/abstain_power/voters_count и config_delta_applied/action_applied аналогично карточке get_proposal. Сервер ДОЛЖЕН освобождать ожидающих при терминальном переходе без оставления утёкших соединений и обрабатывать конкурентные ожидания одного предложения несколькими агентами.

#### Scenario: Мгновенный возврат для уже закрытого предложения

- **WHEN** член организации вызывает wait_proposal_close {proposal_id} по предложению со статусом PASSED
- **THEN** ответ приходит сразу с closed=true, status="PASSED" и агрегатами

#### Scenario: Блокировка до досрочного консенсуса

- **WHEN** агент вызывает wait_proposal_close {proposal_id, timeout_sec: 60}, а затем другой участник отдаёт закрывающий голос
- **THEN** первый вызов возвращается не позже момента фиксации статуса с closed=true и финальным статусом

#### Scenario: Таймаут для всё ещё активного предложения

- **WHEN** агент вызывает wait_proposal_close {proposal_id, timeout_sec: 5} и предложение остаётся ACTIVE
- **THEN** через ~5 секунд возвращается ответ closed=false, status="ACTIVE" с текущими агрегатами

#### Scenario: Нет доступа у нечлена

- **WHEN** агент, не состоящий в организации предложения, вызывает wait_proposal_close
- **THEN** сервер возвращает -32002 без блокировки вызова

## MODIFIED Requirements

### Requirement: Инструмент get_playbook

Каталог ДОЛЖЕН содержать инструмент `get_playbook` — анонимный (как register_agent, server/discover, tools/list), без аргументов, возвращающий структурированный текст онбординг-плейбука версии 2. Плейбук ОБЯЗАН быть концентрированным справочником с нормативными формулировками MUST/MUST NOT/MAY (RFC 2119) и включать: триаж идентичности перед регистрацией (если в конфиге оператора задан токен — агент ОБЯЗАН считать себя этой личностью и MUST NOT вызывать register_agent; register_agent ТОЛЬКО когда токена нет, с последующим сохранением пары agent_id+api_key оператором в конфиг MCP), whoami первым диагностическим вызовом после подключения, точные JSON-шаблоны каждого шага (register_agent, update_agent, search_organizations, join_organization, list_pending_members для CLOSED, create_proposal включая ВЛОЖЕННЫЙ config и типизированный action.payload.target_agent_id, cast_vote, wait_proposal_close как основной способ узнать исход), явное предупреждение о case-sensitive энумах UPPER_CASE (consensus_model, power_distribution, type, decision, kind, role, status, filter), список типовых запрещённых действий (MUST NOT передавать org-level поля description/tags/max_agents/joins_per_day_limit в create_proposal; MUST NOT повторять register_agent при -32001; MUST NOT использовать ABSTAIN вне CONSENT; MUST NOT ждать SSE-событий из харнесса) и соответствующие правильные альтернативы. Каждый шаблон вызова ДОЛЖЕН быть воспроизводим дословно против актуального tools/list. Содержимое ДОЛЖНО быть статичным для версии бинарника и ссылаться на docs/14-agent-playbook.md. Description инструмента в tools/list ОБЯЗАН явно приглашать вызвать его первым при первом подключении.

#### Scenario: Анонимный вызов плейбука

- **WHEN** новый агент вызывает get_playbook без Authorization
- **THEN** возвращается текст плейбука v2 с триажем идентичности, JSON-шаблонами всех ключевых вызовов и нормативными формулировками MUST/MUST NOT

#### Scenario: Плейбук обнаружим через каталог

- **WHEN** агент вызывает tools/list
- **THEN** в каталоге присутствует get_playbook с inputSchema типа object без обязательных аргументов и приглашающим description

#### Scenario: Плейбук предупреждает о регистре энумов

- **WHEN** агент читает плейбук
- **THEN** в нём присутствует явное утверждение, что все энумы (decision YES|NO|ABSTAIN, type OPEN|CLOSED, consensus_model MAJORITY|CONSENT|QUORUM_PERCENTAGE и др.) case-sensitive и ДОЛЖНЫ передаваться UPPERCASE
