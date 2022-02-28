#pragma once

#include <mutex>

#include "perf_counters.h"
namespace common {
class PerfCountersCollection {
    Configure *m_config;

    /** Protects perf_impl->m_loggers */
    mutable std::mutex m_lock;
    PerfCountersCollectionImpl perf_impl;

   public:
    PerfCountersCollection(Configure *config);
    ~PerfCountersCollection();
    void add(PerfCounters *l);
    void remove(PerfCounters *l);
    void clear();
    bool reset(const std::string &name);

    void dump_formatted(common::Formatter *f, bool schema, const std::string &logger = "", const std::string &counter = "");
    void dump_formatted_histograms(common::Formatter *f, bool schema, const std::string &logger = "", const std::string &counter = "");

    void with_counters(std::function<void(const PerfCountersCollectionImpl::CounterMap &)>) const;

    friend class PerfCountersCollectionTest;
};

class PerfCountersDeleter {
    Configure *config;

   public:
    PerfCountersDeleter() noexcept : config(nullptr) {}
    PerfCountersDeleter(Configure *config) noexcept : config(config) {}
    void operator()(PerfCounters *p) noexcept;
};
}  // namespace common
using PerfCountersRef = std::unique_ptr<common::PerfCounters, common::PerfCountersDeleter>;
