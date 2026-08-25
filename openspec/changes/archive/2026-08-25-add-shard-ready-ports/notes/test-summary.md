# Сводка верификации change add-shard-ready-ports

Дата: 2026-08-25 · Сборка: Release, чистая (без санитайзеров), /tmp/opencode/build-rel

## Полный набор (ctest): 198/198 PASSED

Набор | Тестов | Статус
unit (включая ScalingPortsContract.*, Config.Cluster*) | 92 | ✅
integration (включая GoldenRegression.CanonicalScenario) | 80 | ✅
e2e + shutdown | 26 | ✅
seam_isolation (CTest guard, python) | 1 | ✅
ИТОГО ctest | **198** | ✅ 100%

## Ключевые проверки спеки

- Golden-регрессия: fixture снят с ДО-портового кода (git stash → сборка старого ядра →
  генерация), портированное дерево сравнено deep-equal после нормализации UUID/api_key —
  идентично, включая пагинацию курсором и порядок SSE-событий.
- Контрактные тесты портов: одинаковые сценарии на Local* (RocksDB) и фейках — согласованы;
  edge: неизвестный токен, агент без организаций, неизвестный proposal, дубликат createAgent.
- Перф-гейт p99 (<2%): PASS — после перевода быстрее на 4–46% по всем операциям
  (bench-baseline.txt / bench-after.txt; voterpool_hot_path_bench).
- seam_isolation: прямой доступ к OrgNameRegistry/auth-путям вне src/scaling запрещён
  (whitelist: composition root AppContext.cpp, консенсус, storage).

## TSan-прогон контрактных тестов — ЗАБЛОКИРОВАН СРЕДОЙ

Контейнер запрещает syscall personality(ADDR_NO_RANDOMIZE) — TSan падает на старте
любого бинарника (включая hello-world probe):

    ThreadSanitizer: CHECK failed tsan_platform_linux.cpp:315
    setarch: failed to set personality: Operation not permitted

Команда для хоста без этого ограничения:

    cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
          -DVOTERPOOL_BUILD_TESTS=ON \
          -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
    cmake --build build-tsan --target voterpool_unit_tests
    ./build-tsan/tests/voterpool_unit_tests --gtest_filter='ScalingPortsContract.*'

Задача 5.1 остаётся открытой до прогона этой команды на подходящем хосте.
