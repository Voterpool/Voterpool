// Юнит-тесты раскладки keyspaces v2 (бакеты): заморозка алгоритма
// размещения тест-векторами и инвентаризация классификации Keys.h.
#include "scaling/BucketResolver.h"
#include "storage/Keys.h"

#include <gtest/gtest.h>

#include <fstream>
#include <json/json.h>
#include <map>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#ifdef VOTERPOOL_FIXTURES_DIR
#define FIXTURES_DIR VOTERPOOL_FIXTURES_DIR
#else
#define FIXTURES_DIR "../fixtures"
#endif
#ifdef VOTERPOOL_ROOT_DIR
#define ROOT_DIR VOTERPOOL_ROOT_DIR
#else
#define ROOT_DIR "../.."
#endif

using namespace voterpool;

namespace {
std::string loadFile(const std::string& rel) {
    std::ifstream in(std::string(FIXTURES_DIR) + "/" + rel);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

// Заморозка алгоритма: fnv1a64(org_id) mod 256, векторы включают границы 0/255.
TEST(BucketResolver, VectorsFrozenAndDeterministic) {
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream in(loadFile("bucket_vectors.json"));
    ASSERT_TRUE(Json::parseFromStream(rb, in, &root, &errs)) << errs;
    ASSERT_EQ(root["algorithm"].asString(), "fnv1a64_mod_256");
    for (const auto& v : root["vectors"]) {
        EXPECT_EQ(scaling::bucketFor(v["org_id"].asString()), v["bucket"].asInt())
            << "org_id=" << v["org_id"].asString();
        // Двойной вызов — чистая функция.
        EXPECT_EQ(scaling::bucketFor(v["org_id"].asString()),
                  scaling::bucketFor(v["org_id"].asString()));
    }
}

TEST(BucketResolver, PrefixFormatIsThreeDigits) {
    EXPECT_EQ(scaling::bucketPrefix(0), "b000:");
    EXPECT_EQ(scaling::bucketPrefix(7), "b007:");
    EXPECT_EQ(scaling::bucketPrefix(127), "b127:");
    EXPECT_EQ(scaling::bucketPrefix(255), "b255:");
    // Все 256 префиксов уникальны и корректны.
    std::set<std::string> uniq;
    for (int b = 0; b < scaling::kBucketCount; ++b) uniq.insert(scaling::bucketPrefix(b));
    EXPECT_EQ(uniq.size(), static_cast<size_t>(scaling::kBucketCount));
}

// Орг-плоскость: префикс бакета выведен из org_id (не агента!).
TEST(KeysV2, OrgScopedKeysCarryOwnerBucket) {
    const int bo = Keys::bucketFor("o1");
    const int bo9 = Keys::bucketFor("o9");
    const std::string p1 = scaling::bucketPrefix(bo);
    const std::string p9 = scaling::bucketPrefix(bo9);

    EXPECT_EQ(Keys::org("o1"), p1 + "org:o1");
    EXPECT_EQ(Keys::membership("o1", "a1"), p1 + "org:o1:member:a1");
    EXPECT_EQ(Keys::membershipPrefix("o1"), p1 + "org:o1:member:");
    EXPECT_EQ(Keys::proposal("o1", "p1"), p1 + "org:o1:proposal:p1");
    EXPECT_EQ(Keys::vote("o1", "p1", "a2"), p1 + "org:o1:proposal:p1:vote:a2");
    // agent_orgs живёт в БАКЕТЕ ОРГАНИЗАЦИИ, не агента.
    EXPECT_EQ(Keys::agentOrgs("a1", "o9"), p9 + "agent_orgs:a1:o9");
    EXPECT_EQ(Keys::pending("o1", "a1"), p1 + "pending:o1:a1");
    EXPECT_EQ(Keys::joinLimit("o1", 1700000000), p1 + "join_limit:o1:" + Keys::yyyymmdd(1700000000));
    EXPECT_TRUE(Keys::auditPrefix("o1").rfind(p1 + "audit:o1:", 0) == 0);
}

// Системная плоскость: без префикса бакета.
TEST(KeysV2, SystemPlaneKeysUnscoped) {
    EXPECT_EQ(Keys::agent("x"), "agent:x");
    EXPECT_EQ(Keys::auth("h"), "auth:h");
    EXPECT_EQ(Keys::proposalLookup("p"), "proposal_lookup:p");
}

TEST(KeysV2, ActiveProposalPaddedOrderWithinOneOrg) {
    const char* org = "same-org";
    std::string small = Keys::activeProposal(org, 1000, "p");
    std::string large = Keys::activeProposal(org, 20000000000, "p");
    ASSERT_EQ(small.size(), large.size());
    EXPECT_LT(small, large);
}

TEST(KeysV2, FeedWithinOneBucketNewestFirst) {
    std::string older = Keys::orgFeed("ACTIVE", 1700000000, "same");
    std::string newer = Keys::orgFeed("ACTIVE", 1799999999, "same");
    EXPECT_LT(newer, older);  // rev-таймстамп: меньше rev = новее
}

TEST(KeysV2, TagsLowercasedAndScoped) {
    EXPECT_EQ(Keys::tagLower("InfRa"), "infra");
    EXPECT_EQ(Keys::nameLower("AI Council"), "ai council");
    const std::string p = scaling::bucketPrefix(Keys::bucketFor("o"));
    EXPECT_EQ(Keys::tag("infra", "o"), p + "tag:infra:o");
    EXPECT_EQ(Keys::category(Keys::nameLower("Governance"), "o"),
              p + "category:governance:o");
}

TEST(KeysV2, AuditKeyFixedWidthFieldsUnderBucketPrefix) {
    const std::string p = scaling::bucketPrefix(Keys::bucketFor("o"));
    std::string k = Keys::auditKey("o", 1697056500123, 0xdeadbeefULL, 5);
    EXPECT_EQ(k, p + "audit:o:00000001697056500123:00000000deadbeef0000000000000000005");

    // Хронологический порядок внутри одной миллисекунды сохранён.
    std::string prev = k;
    for (std::int64_t seq = 6; seq <= 1500; ++seq) {
        std::string n = Keys::auditKey("o", 1697056500123, 0xdeadbeefULL, seq);
        ASSERT_LT(prev, n);
        prev = n;
    }
}

// ---- Перенесено из legacy test_keys.cpp (удалён при миграции раскладки) ----

// Хронология аудита как самостоятельный кейс: фиксированная ширина seq даёт
// лексикографический порядок == порядку записи; граница миллисекунды строго после.
TEST(KeysLegacyPorted, AuditKeysSortChronologicallyInsideSameMillisecond) {
    const std::string p = scaling::bucketPrefix(Keys::bucketFor("o"));
    std::string prev = Keys::auditKey("o", 1697056500123, 0xAABBCCDDULL, 0);
    for (std::int64_t seq = 1; seq <= 1500; ++seq) {
        std::string k = Keys::auditKey("o", 1697056500123, 0xAABBCCDDULL, seq);
        ASSERT_LT(prev, k) << "seq " << seq;
        prev = k;
    }
    std::string later = Keys::auditKey("o", 1697056500124, 0xAABBCCDDULL, 0);
    EXPECT_LT(prev, later) << "later millisecond sorts after all of the previous one";
}

// Префикс-скан listByOrg не зависит от формата хвоста: записи, мигрировавшие
// в бакет со СТАРЫМ коротким хвостом (до паддинга), остаются в диапазоне
// b{NNN}:audit:{org}: — миграция v2→v3 переносит ключ целиком (docs/12 §4.1).
TEST(KeysLegacyPorted, AuditPrefixScanIsIndependentOfTail) {
    const int b = Keys::bucketFor("org-1");
    std::string modern = Keys::auditKey("org-1", 1697056500123, 1234567890123456789ULL, 42);
    // Легаси-запись после миграции: тот же бакет, прежний короткий хвост.
    const std::string legacyMigrated =
        scaling::scoped(b, "audit:org-1:0000001697056500123:7");
    for (const std::string& k : {modern, legacyMigrated}) {
        ASSERT_EQ(k.rfind(scaling::bucketPrefix(b) + "audit:org-1:", 0), 0u) << k;
    }
    EXPECT_NE(modern, legacyMigrated);
}

// Дневной штамп лимита вступлений — UTC-дата; литерал фиксирует таймзону.
TEST(KeysLegacyPorted, JoinLimitUsesUtcDayStamp) {
    const std::int64_t ts = 1700000000;  // 2023-11-14 22:13:20 UTC
    EXPECT_EQ(Keys::yyyymmdd(ts), "20231114");
    const std::string p = scaling::bucketPrefix(Keys::bucketFor("o1"));
    EXPECT_EQ(Keys::joinLimit("o1", ts), p + "join_limit:o1:20231114");
}

// Инвентаризация: каждая inline-функция Keys.h классифицирована
// в манифесте как scoped|system. Новая функция без записи ломает тест.
TEST(KeysV2, InventoryManifestCoversEveryConstructor) {
    // 1. Прочитать манифест.
    std::map<std::string, std::string> manifest;
    std::ifstream mf(std::string(FIXTURES_DIR) + "/keyscope_manifest.txt");
    ASSERT_TRUE(mf.good()) << "missing keyscope_manifest.txt";
    std::string line;
    while (std::getline(mf, line)) {
        if (auto h = line.find('#'); h != std::string::npos) line = line.substr(0, h);
        std::istringstream ls(line);
        std::string name, kind;
        if (!(ls >> name >> kind)) continue;
        manifest[name] = kind;
    }

    // 2. Распарсить Keys.h: имена inline std::string функций.
    std::ifstream src(std::string(ROOT_DIR) + "/include/storage/Keys.h");
    ASSERT_TRUE(src.good());
    std::string text((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
    std::regex re(R"(inline\s+std::string\s+(\w+)\s*\()");
    std::set<std::string> declared;
    auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) declared.insert((*it)[1]);

    ASSERT_FALSE(declared.empty()) << "parse failure of Keys.h";

    std::vector<std::string> problems;
    for (const auto& d : declared) {
        if (!manifest.count(d)) problems.push_back("unclassified: " + d);
    }
    for (const auto& [n, k] : manifest) {
        if (!declared.count(n)) problems.push_back("stale manifest entry: " + n);
        else if (k != "scoped" && k != "system") problems.push_back("bad kind: " + n);
    }
    for (const auto& pr : problems) ADD_FAILURE() << pr;

    // 3. Кросс-проверка семантики на репрезентативных входах.
    const std::string probeOrg = "inventory-org";
    const std::string pref = scaling::bucketPrefix(Keys::bucketFor(probeOrg));
    EXPECT_EQ(Keys::org(probeOrg).rfind(pref, 0), 0u)
        << "scoped constructor missing bucket prefix";
    EXPECT_NE(Keys::agent("probe").rfind(pref, 0), 0u);
    EXPECT_NE(Keys::auth("probe").rfind(pref, 0), 0u);
    EXPECT_NE(Keys::proposalLookup("probe").rfind(pref, 0), 0u);
}
