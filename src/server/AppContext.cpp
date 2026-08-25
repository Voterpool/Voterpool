#include "server/AppContext.h"

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
    if (gate == SchemaGateResult::kError) {
        throw std::runtime_error("Database schema migration failed; exiting");
    }
    agents = std::make_unique<AgentRepository>(*db, *clock);
    orgs = std::make_unique<OrgRepository>(*db, *clock);
    proposals = std::make_unique<ProposalRepository>(*db, *clock);
    votes = std::make_unique<VoteRepository>(*db, *clock);
    indexes = std::make_unique<IndexRepository>(*db);
    audit = std::make_unique<AuditLogRepository>(*db);
    // Gauge агентов инициализируется состоянием БД: после рестарта на
    // непустой базе /metrics отдаёт корректное число с первого скрейпа.
    MetricsRegistry::instance().setGauge("voterpool_agents_total", {},
                                         static_cast<std::int64_t>(agents->count()));
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
    hub = std::make_unique<SseHub>();
    orgNames = std::make_unique<OrgNameRegistry>();
    orgNames->load(*db);
    spdlog::info("Loaded organization name registry: {} entries", orgNames->size());
    directory = std::make_unique<scaling::LocalDirectory>(*orgNames, *indexes, *orgs, *proposals);
    identity = std::make_unique<scaling::LocalIdentity>(*agents, *orgs);
    events = std::make_unique<scaling::LocalEventBus>(*hub);
    engine = std::make_unique<ConsensusEngine>(ConsensusEngine::Deps{
        db.get(), orgs.get(), proposals.get(), votes.get(),
        indexes.get(), audit.get(), &locks, &orgLocks, clock, orgNames.get(),
        [this](const SseEvent& ev) {
            if (workers) workers->enqueue(ev);
            else events->deliver(ev);
        }});
}

}  // namespace voterpool
