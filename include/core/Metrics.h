#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace voterpool {

class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    void incCounter(const std::string& name, const std::vector<std::pair<std::string, std::string>>& labels = {}, std::int64_t delta = 1);
    void decCounter(const std::string& name, const std::vector<std::pair<std::string, std::string>>& labels = {}) { incCounter(name, labels, -1); }
    void setGauge(const std::string& name, const std::vector<std::pair<std::string, std::string>>& labels, std::int64_t value);
    void observe(const std::string& name, double seconds);

    void addLabelHelp(const std::string& name, const std::string& type, const std::string& help);

    std::string expose() const;

    void resetForTests();

private:
    MetricsRegistry() = default;

    struct Series {
        std::vector<std::pair<std::string, std::string>> labels;
        std::atomic<std::int64_t> value{0};
    };
    struct MetricDef {
        std::string type;
        std::string help;
        std::mutex mutex;
        std::map<std::string, std::shared_ptr<Series>> series;
    };

    MetricDef& getMetric(const std::string& name);
    static std::shared_ptr<Series> seriesFor(MetricDef& m, const std::vector<std::pair<std::string, std::string>>& labels);
    static std::string labelKey(const std::vector<std::pair<std::string, std::string>>& labels);
    static std::string formatLabels(const std::vector<std::pair<std::string, std::string>>& labels);

    mutable std::mutex registryMutex_;
    std::map<std::string, std::shared_ptr<MetricDef>> metrics_;

    struct HistogramDef {
        std::vector<double> buckets;
        std::vector<std::shared_ptr<std::atomic<std::int64_t>>> bucketCounts;
        std::shared_ptr<std::atomic<std::int64_t>> count;
        std::shared_ptr<std::atomic<std::int64_t>> sumMicros;
        std::string help;
    };
    mutable std::mutex histMutex_;
    std::map<std::string, std::shared_ptr<HistogramDef>> histograms_;
};

using Metrics = MetricsRegistry;

}  // namespace voterpool
