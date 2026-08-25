# Delta: organizations

## MODIFIED Requirements

### Requirement: Создание организации

Инструмент create_organization ДОЛЖЕН принимать name, short_description, description, tags, category (опциональная строка, единое значение), type (OPEN|CLOSED), max_agents (0 = без лимита), joins_per_day_limit (0 = без лимита) и config {consensus_model: MAJORITY|CONSENT|QUORUM_PERCENTAGE, quorum_percentage 0–100, voting_duration_sec > 0, power_distribution: EQUAL|SHARES}. Переданная category ДОЛЖНА сохраняться на организации и индексироваться тем же вторичным индексом, что используется после UPDATE_ORG_INFO, так что search_organizations с фильтром по этой категории ДОЛЖЕН находить организацию СРАЗУ после создания (без промежуточного голосования); отсутствие category в аргументах эквивалентно пустой строке (организация не находится фильтром по категории). Конфигурация, сочетающая consensus_model CONSENT с power_distribution SHARES, ДОЛЖНА отклоняться ошибкой -32005: модель CONSENT предполагает равенство всех голосов, вариант SHARES для неё отсутствует. То же ограничение ДОЛЖНО применяться к итоговой конфигурации при config_delta в create_proposal. config_delta в create_proposal ДОЛЖЕН интерпретироваться как частичная дельта поверх действующей конфигурации организации: отсутствующие поля наследуются из текущей конфигурации организации, а не из значений по умолчанию; проверка ограничений и последующее применение выполняются к смерженному итогу; применение PASSED-предложения записывает в организацию смерженный конфиг целиком. Создатель ОБЯЗАН автоматически становиться единственным ADMIN; voting_power создателя — 100.0 при SHARES и 1.0 при EQUAL; total_voting_power организации инициализируется силой создателя. Ответ ДОЛЖЕН содержать org_id, данные организации (включая category), role ADMIN и voting_power. Конфигурация с voting_duration_sec <= 0 ДОЛЖНА отклоняться ошибкой -32005.

#### Scenario: Создание CLOSED-организации с SHARES

- **WHEN** агент создаёт организацию type CLOSED с power_distribution SHARES
- **THEN** ответ содержит org_id, role "ADMIN", voting_power 100.0 и переданный config

#### Scenario: Нулевая длительность голосования

- **WHEN** create_organization передаёт config.voting_duration_sec = 0
- **THEN** сервер возвращает ошибку -32005 Business Rule Violation

#### Scenario: Дубликат названия

- **WHEN** агент создаёт организацию с названием уже существующей организации
- **THEN** сервер возвращает ошибку -32003 Conflict

#### Scenario: CONSENT с SHARES отклоняется

- **WHEN** create_organization передаёт config {consensus_model: "CONSENT", power_distribution: "SHARES"}
- **THEN** сервер возвращает ошибку -32005 Business Rule Violation, организация не создаётся

#### Scenario: Смена распределения на SHARES при активном CONSENT отклоняется

- **WHEN** create_proposal передаёт config_delta {consensus_model: "CONSENT", power_distribution: "SHARES"} (или меняет только power_distribution на SHARES в организации с CONSENT)
- **THEN** сервер возвращает ошибку -32005 Business Rule Violation

#### Scenario: Частичная дельта наследует действующую конфигурацию

- **WHEN** create_proposal передаёт config_delta {voting_duration_sec: 7200} в организации с конфигурацией CONSENT / EQUAL / quorum 51 / duration 3600, и предложение проходит
- **THEN** конфигурация организации становится CONSENT / EQUAL / quorum 51 / duration 7200 — модель, распределение и quorum не сбрасываются к значениям по умолчанию

#### Scenario: Частичная дельта с недопустимым значением отклоняется

- **WHEN** create_proposal передаёт config_delta {quorum_percentage: 150} в организации с quorum 51
- **THEN** сервер возвращает ошибку параметров, предложение не создаётся

#### Scenario: Проверка дубликата имени не зависит от механики поиска

- **WHEN** агент создаёт организацию с названием (в любом регистре) уже существующей ACTIVE-организации
- **THEN** сервер возвращает ошибку -32003 Conflict

#### Scenario: Category индексируется сразу при создании

- **WHEN** агент создаёт организацию с category "Governance"
- **THEN** ответ содержит category "Governance"; get_organization возвращает её; search_organizations {category: "governance"} находит организацию немедленно после создания, без каких-либо промежуточных голосований

#### Scenario: Category регистронезависима для поиска

- **WHEN** организация создана с category "Infra", затем выполняется search_organizations {category: "infra"}
- **THEN** организация найдена (сравнение категории в индексе регистронезависимо)

#### Scenario: Category не передана

- **WHEN** агент создаёт организацию без поля category
- **THEN** организация создаётся успешно, get_organization возвращает пустую category, поиск по любой конкретной категории её не находит

#### Scenario: Category неверного типа отклоняется

- **WHEN** create_organization передаёт category массивом или числом
- **THEN** сервер возвращает ошибку -32602 Invalid params
