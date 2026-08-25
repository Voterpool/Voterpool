#include "storage/repositories/AuditLogRepository.h"
#include "tests/common/Harness.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

AuditEvent makeEvent(const std::string& orgId, std::int64_t tsMs, const std::string& tag) {
    AuditEvent e;
    e.action = "POWER_CHANGED";
    e.org_id = orgId;
    e.agent_id = "target-" + tag;
    e.by_agent = "admin-1";
    e.old_power = 1.0;
    e.new_power = 2.0;
    e.created_at = tsMs;
    return e;
}

}  // namespace

// data-persistence «Более 1000 событий за одну миллисекунду»: прежний ключ
// (seq % 1000 в RAM) затирал записи начиная с 1001-й — теперь все читаемы.
TEST(AuditJournal, ThousandPlusEventsSameMillisecondAllSurvive) {
    auto h = Harness::create();
    constexpr int kEvents = 1500;
    const std::int64_t tsMs = 1697056500123;

    AuditLogRepository audit(*h->app->db);
    {
        rocksdb::WriteBatch batch;
        for (int i = 0; i < kEvents; ++i) {
            audit.append(batch, "org-1", makeEvent("org-1", tsMs, std::to_string(i)));
        }
        ASSERT_TRUE(h->app->db->commit(batch));
    }

    auto events = audit.listByOrg("org-1");
    ASSERT_EQ(events.size(), kEvents) << "no event may be overwritten by a key collision";
}

// data-persistence «Перезапуск не создаёт коллизий»: второй экземпляр
// репозитория имитирует рестарт процесса (новая соль, seq снова с нуля).
TEST(AuditJournal, RestartedInstanceDoesNotOverwriteHistory) {
    auto h = Harness::create();
    const std::int64_t tsMs = 1697056500999;

    std::vector<AuditEvent> firstBatch;
    {
        AuditLogRepository firstRun(*h->app->db);
        rocksdb::WriteBatch batch;
        for (int i = 0; i < 50; ++i) {
            AuditEvent e = makeEvent("org-rs", tsMs, "first-" + std::to_string(i));
            firstBatch.push_back(e);
            firstRun.append(batch, "org-rs", e);
        }
        ASSERT_TRUE(h->app->db->commit(batch));
    }

    {
        // «Рестарт»: seq начинается с нуля, salt другой — в ту же миллисекунду.
        AuditLogRepository restarted(*h->app->db);
        rocksdb::WriteBatch batch;
        for (int i = 0; i < 50; ++i) {
            restarted.append(batch, "org-rs", makeEvent("org-rs", tsMs, "second-" + std::to_string(i)));
        }
        ASSERT_TRUE(h->app->db->commit(batch));
    }

    AuditLogRepository reader(*h->app->db);
    auto events = reader.listByOrg("org-rs");
    ASSERT_EQ(events.size(), 100u) << "history must survive the restart untouched";

    int firstSeen = 0, secondSeen = 0;
    for (const auto& e : events) {
        if (e.agent_id.rfind("target-first-", 0) == 0) ++firstSeen;
        if (e.agent_id.rfind("target-second-", 0) == 0) ++secondSeen;
    }
    EXPECT_EQ(firstSeen, 50);
    EXPECT_EQ(secondSeen, 50);

    // Прежние записи побайтово не изменились (проверяем содержимое первой).
    bool foundFirstRecord = false;
    for (const auto& e : events) {
        if (e.agent_id == "target-first-7") {
            foundFirstRecord = true;
            EXPECT_EQ(e.action, firstBatch[7].action);
            EXPECT_EQ(e.by_agent, firstBatch[7].by_agent);
            EXPECT_DOUBLE_EQ(e.old_power, firstBatch[7].old_power);
            EXPECT_DOUBLE_EQ(e.new_power, firstBatch[7].new_power);
        }
    }
    EXPECT_TRUE(foundFirstRecord);
}

// data-persistence «Хронологический порядок внутри одной миллисекунды»:
// лексикографический порядок ключей одного запуска совпадает с порядком
// записи (seq фиксированной ширины), listByOrg отдаёт события по времени.
TEST(AuditJournal, WithinOneBootReadingOrderMatchesWritingOrder) {
    auto h = Harness::create();

    AuditLogRepository audit(*h->app->db);
    rocksdb::WriteBatch batch;
    // Разные миллисекунды и несколько событий внутри одной миллисекунды.
    for (int i = 0; i < 30; ++i) {
        std::int64_t ms = 1700000000000LL + (i / 3) * 5;
        audit.append(batch, "org-order", makeEvent("org-order", ms, std::to_string(i)));
    }
    ASSERT_TRUE(h->app->db->commit(batch));

    auto events = audit.listByOrg("org-order");
    ASSERT_EQ(events.size(), 30u);
    for (int i = 0; i < 30; ++i) {
        EXPECT_EQ(events[i].agent_id, "target-" + std::to_string(i))
            << "event " << i << " out of order";
        if (i > 0) {
            EXPECT_LE(events[i - 1].created_at, events[i].created_at);
        }
    }
}
