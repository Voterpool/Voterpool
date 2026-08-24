#include "server/AppContext.h"

#include "server/NativeAuthProvider.h"
#include "storage/SchemaVersion.h"

#include <spdlog/spdlog.h>

namespace voterpool {

void AppContext::init(IClock* clockOverride) {
    MetricsRegistry::instance().registerDefaults();
    clock = clockOverride ? clockOverride : &SystemClock::instance();
    db = std::make_unique<RocksDBWrapper>(config.storage);
    if (!db->open()) {
        throw std::runtime_error("Failed to open RocksDB at " + config.storage.path);
    }
    SchemaManager schema(*db);
    auto gate = schema.run();
    if (gate == SchemaGateResult::kFatalNewerSchema) {
        throw std::runtime_error("Database schema is newer than the binary; exiting");
    }
    agents = std::make_unique<AgentRepository>(*db, *clock);
    orgs = std::make_unique<OrgRepository>(*db, *clock);
    proposals = std::make_unique<ProposalRepository>(*db, *clock);
    votes = std::make_unique<VoteRepository>(*db, *clock);
    indexes = std::make_unique<IndexRepository>(*db);
    audit = std::make_unique<AuditLogRepository>(*db);
    if (config.storage.rebuild_index_on_start) {
        std::size_t restored = 0;
        rocksdb::WriteBatch batch;
        for (const auto& p : proposals->listByOrgAll()) {
            if (p.status == ProposalStatus::ACTIVE) {
                proposals->addActiveIndex(batch, p);
                ++restored;
            }
        }
        db->commit(batch);
        spdlog::info("Rebuilt active-proposals index: {} entries", restored);
    }
    authProvider = std::make_unique<NativeAuthProvider>(*agents);
    hub = std::make_unique<SseHub>();
    orgNames = std::make_unique<OrgNameRegistry>();
    orgNames->load(*db);
    spdlog::info("Loaded organization name registry: {} entries", orgNames->size());
    engine = std::make_unique<ConsensusEngine>(ConsensusEngine::Deps{
        db.get(), orgs.get(), proposals.get(), votes.get(),
        indexes.get(), audit.get(), &locks, &orgLocks, clock, orgNames.get(),
        [this](const SseEvent& ev) {
            if (workers) workers->enqueue(ev);
            else hub->deliver(ev);
        }});
}

}  // namespace voterpool
