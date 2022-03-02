#pragma once

#include <functional>
#include <mutex>

#include "common/formatter.h"
#include "perf_counters.h"

namespace common::PerfCounter {
class PerfCountersCollection {
    Context *m_context;

    /** Protects perf_impl->m_loggers */
    mutable std::mutex m_lock;
    PerfCounter::PerfCountersCollectionImpl perf_impl;

   public:
    PerfCountersCollection(Context *context);
    ~PerfCountersCollection();
    void add(PerfCounter::PerfCounters *l);
    void remove(PerfCounter::PerfCounters *l);
    void clear();
    bool reset(const std::string &name);

    void dump_formatted(common::Formatter *f, bool schema, const std::string &logger = "", const std::string &counter = "");
    void dump_formatted_histograms(common::Formatter *f, bool schema, const std::string &logger = "", const std::string &counter = "");

    void with_counters(std::function<void(const PerfCountersCollectionImpl::CounterMap &)>) const;

    friend class PerfCountersCollectionTest;
};

class PerfCountersDeleter {
    Context *config;

   public:
    PerfCountersDeleter() noexcept : config(nullptr) {}
    PerfCountersDeleter(Context *config) noexcept : config(config) {}
    void operator()(PerfCounters *p) noexcept;
};
}  // namespace common::PerfCounter
using PerfCountersRef = std::unique_ptr<common::PerfCounter::PerfCounters, common::PerfCounter::PerfCountersDeleter>;
