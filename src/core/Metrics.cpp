#include "core/Metrics.h"

#include <algorithm>
#include <sstream>

namespace voterpool {

namespace {

// Каталог метрик docs/11 §3: имя → тип и человекочитаемое описание (# HELP).
struct MetricCatalogEntry {
    const char* name;
    const char* type;
    const char* help;
};

const MetricCatalogEntry kCatalog[] = {
    {"voterpool_actions_applied_total", "counter", "Applied ACTION proposals by kind"},
    {"voterpool_agents_total", "gauge", "Registered agents"},
    {"voterpool_consensus_early_exit_total", "counter", "Early-Exit optimization triggers during voting"},
    {"voterpool_db_healthy", "gauge", "1 = storage healthy; 0 = degraded"},
    {"voterpool_db_write_failures_total", "counter", "Storage write failures by kind"},
    {"voterpool_http_request_duration_seconds", "histogram", "POST /mcp request latency"},
    {"voterpool_mcp_client_meta_total", "counter", "Requests with/without _meta clientInfo"},
    {"voterpool_mcp_requests_total", "counter", "MCP calls by method, tool and outcome"},
    {"voterpool_orgs_active", "gauge", "ACTIVE organizations"},
    {"voterpool_orgs_dissolved_total", "counter", "Organization dissolutions"},
    {"voterpool_proposals_active", "gauge", "Active proposals"},
    {"voterpool_proposals_closed_total", "counter", "Closed proposals by final status"},
    {"voterpool_proposals_created_total", "counter", "Created proposals"},
    {"voterpool_rpc_errors_total", "counter", "RPC errors by JSON-RPC error code"},
    {"voterpool_schema_migration_records_total", "counter", "Records transformed by schema migrations"},
    {"voterpool_sse_connections", "gauge", "Active SSE streams"},
    {"voterpool_sse_events_sent_total", "counter", "Delivered SSE events by event type"},
    {"voterpool_sse_queue_depth", "gauge", "SSE dispatcher queue depth"},
    {"voterpool_sse_write_failures_total", "counter", "Failed SSE writes by event type"},
    {"voterpool_cast_vote_duration_seconds", "histogram", "End-to-end cast_vote duration (lock to commit)"},
    {"voterpool_ttl_scan_duration_seconds", "histogram", "TTL active-proposal index scan duration"},
    {"voterpool_ttl_scans_total", "counter", "TTL worker scan cycles"},
    {"voterpool_votes_cast_total", "counter", "Accepted votes by decision"},
    {"voterpool_rocksdb_block_cache_usage", "gauge", "RocksDB block cache usage in bytes"},
    {"voterpool_rocksdb_block_cache_capacity", "gauge", "RocksDB block cache capacity in bytes"},
    {"voterpool_rocksdb_block_cache_hits_total", "gauge", "RocksDB block cache hits"},
    {"voterpool_rocksdb_block_cache_misses_total", "gauge", "RocksDB block cache misses"},
    {"voterpool_rocksdb_estimate_pending_compaction_bytes", "gauge", "Estimated bytes pending compaction"},
    {"voterpool_rocksdb_wal_synced_total", "gauge", "WAL file syncs"},
    {"voterpool_rocksdb_flush_write_bytes_total", "gauge", "Bytes written by memtable flushes"},
    {"voterpool_rocksdb_compaction_read_bytes_total", "gauge", "Bytes read by compactions"},
    {"voterpool_rocksdb_compaction_write_bytes_total", "gauge", "Bytes written by compactions"},
};

std::string metricHelp(const std::string& name) {
    for (const auto& e : kCatalog) {
        if (name == e.name) return e.help;
    }
    return name;
}

std::string metricType(const std::string& name, const std::string& fallback) {
    for (const auto& e : kCatalog) {
        if (name == e.name) return e.type;
    }
    return fallback;
}

}  // namespace

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry inst;
    return inst;
}

std::string MetricsRegistry::labelKey(const std::vector<std::pair<std::string, std::string>>& labels) {
    std::string key;
    for (const auto& [k, v] : labels) {
        key += k;
        key.push_back('\x01');
        key += v;
        key.push_back('\x02');
    }
    return key;
}

std::string MetricsRegistry::formatLabels(const std::vector<std::pair<std::string, std::string>>& labels) {
    if (labels.empty()) return {};
    std::string out = "{";
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i) out += ",";
        out += labels[i].first + "=\"" + labels[i].second + "\"";
    }
    out += "}";
    return out;
}

MetricsRegistry::MetricDef& MetricsRegistry::getMetric(const std::string& name) {
    std::lock_guard lock(registryMutex_);
    auto& m = metrics_[name];
    if (!m) m = std::make_shared<MetricDef>();
    return *m;
}

void MetricsRegistry::addLabelHelp(const std::string& name, const std::string& type, const std::string& help) {
    MetricDef& m = getMetric(name);
    std::lock_guard lock(m.mutex);
    m.type = type;
    m.help = help;
}

void MetricsRegistry::registerDefaults() {
    for (const auto& e : kCatalog) {
        if (std::string(e.type) == "histogram") continue;  // гистограммы живут в своём реестре
        addLabelHelp(e.name, e.type, e.help);
    }
}

std::shared_ptr<MetricsRegistry::Series> MetricsRegistry::seriesFor(MetricDef& m, const std::vector<std::pair<std::string, std::string>>& labels) {
    std::string key = labelKey(labels);
    std::lock_guard lock(m.mutex);
    auto it = m.series.find(key);
    if (it != m.series.end()) return it->second;
    auto s = std::make_shared<Series>();
    s->labels = labels;
    m.series.emplace(key, s);
    return s;
}

void MetricsRegistry::incCounter(const std::string& name, const std::vector<std::pair<std::string, std::string>>& labels, std::int64_t delta) {
    MetricDef& m = getMetric(name);
    if (m.type.empty()) addLabelHelp(name, metricType(name, "counter"), metricHelp(name));
    seriesFor(m, labels)->value.fetch_add(delta, std::memory_order_relaxed);
}

void MetricsRegistry::setGauge(const std::string& name, const std::vector<std::pair<std::string, std::string>>& labels, std::int64_t value) {
    MetricDef& m = getMetric(name);
    if (m.type.empty()) addLabelHelp(name, metricType(name, "gauge"), metricHelp(name));
    seriesFor(m, labels)->value.store(value, std::memory_order_relaxed);
}

void MetricsRegistry::observe(const std::string& name, double seconds) {
    static const double kBuckets[] = {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0};
    constexpr size_t kNumBuckets = sizeof(kBuckets) / sizeof(kBuckets[0]);

    std::shared_ptr<HistogramDef> h;
    {
        std::lock_guard lock(histMutex_);
        auto it = histograms_.find(name);
        if (it == histograms_.end()) {
            auto def = std::make_shared<HistogramDef>();
            def->help = metricHelp(name);
            for (size_t i = 0; i < kNumBuckets; ++i) {
                def->buckets.push_back(kBuckets[i]);
                def->bucketCounts.push_back(std::make_shared<std::atomic<std::int64_t>>(0));
            }
            def->count = std::make_shared<std::atomic<std::int64_t>>(0);
            def->sumMicros = std::make_shared<std::atomic<std::int64_t>>(0);
            histograms_.emplace(name, def);
            h = def;
        } else {
            h = it->second;
        }
    }
    auto micros = static_cast<std::int64_t>(seconds * 1e6);
    h->sumMicros->fetch_add(micros, std::memory_order_relaxed);
    h->count->fetch_add(1, std::memory_order_relaxed);
    for (size_t i = 0; i < kNumBuckets; ++i) {
        if (seconds <= h->buckets[i]) {
            h->bucketCounts[i]->fetch_add(1, std::memory_order_relaxed);
        }
    }
}

std::string MetricsRegistry::expose() const {
    std::ostringstream out;
    std::map<std::string, std::shared_ptr<MetricDef>> metrics;
    {
        std::lock_guard lock(registryMutex_);
        metrics = metrics_;
    }
    for (const auto& [name, def] : metrics) {
        std::lock_guard lock(def->mutex);
        // Конвенция exposition format: сначала # HELP, затем # TYPE.
        out << "# HELP " << name << " " << (!def->help.empty() ? def->help : metricHelp(name)) << "\n";
        out << "# TYPE " << name << " " << def->type << "\n";
        for (const auto& [key, s] : def->series) {
            out << name << formatLabels(s->labels) << " " << s->value.load(std::memory_order_relaxed) << "\n";
        }
        if (def->series.empty()) {
            out << name << " 0\n";
        }
    }

    std::map<std::string, std::shared_ptr<HistogramDef>> hists;
    {
        std::lock_guard lock(histMutex_);
        hists = histograms_;
    }
    for (const auto& [name, h] : hists) {
        out << "# HELP " << name << " " << (!h->help.empty() ? h->help : metricHelp(name)) << "\n";
        out << "# TYPE " << name << " histogram\n";
        for (size_t i = 0; i < h->buckets.size(); ++i) {
            out << name << "_bucket{le=\"" << h->buckets[i] << "\"} " << h->bucketCounts[i]->load(std::memory_order_relaxed) << "\n";
        }
        out << name << "_bucket{le=\"+Inf\"} " << h->count->load(std::memory_order_relaxed) << "\n";
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", h->sumMicros->load(std::memory_order_relaxed) / 1e6);
        out << name << "_sum " << buf << "\n";
        out << name << "_count " << h->count->load(std::memory_order_relaxed) << "\n";
    }
    return out.str();
}

void MetricsRegistry::resetForTests() {
    {
        std::lock_guard lock(registryMutex_);
        metrics_.clear();
    }
    std::lock_guard lock(histMutex_);
    histograms_.clear();
}

}  // namespace voterpool
