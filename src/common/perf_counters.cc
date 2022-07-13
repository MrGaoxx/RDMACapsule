// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 * Copyright (C) 2017 OVH
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "perf_counters.h"

#include "common.h"
#include "common/context.h"

using std::make_pair;
using std::ostringstream;
using std::pair;

namespace common::PerfCounter {
PerfCountersCollectionImpl::PerfCountersCollectionImpl() {}

PerfCountersCollectionImpl::~PerfCountersCollectionImpl() { clear(); }

void PerfCountersCollectionImpl::add(PerfCounters *l) {
    // make sure the name is unique
    perf_counters_set_t::iterator i;
    i = m_loggers.find(l);
    while (i != m_loggers.end()) {
        ostringstream ss;
        ss << l->get_name() << "-" << (void *)l;
        l->set_name(ss.str());
        i = m_loggers.find(l);
    }

    m_loggers.insert(l);

    for (unsigned int i = 0; i < l->m_data.size(); ++i) {
        PerfCounters::perf_counter_data_any_d &data = l->m_data[i];

        std::string path = l->get_name();
        path += ".";
        path += data.name;

        by_path[path] = {&data, l};
    }
}

void PerfCountersCollectionImpl::remove(PerfCounters *l) {
    for (unsigned int i = 0; i < l->m_data.size(); ++i) {
        PerfCounters::perf_counter_data_any_d &data = l->m_data[i];

        std::string path = l->get_name();
        path += ".";
        path += data.name;

        by_path.erase(path);
    }

    perf_counters_set_t::iterator i = m_loggers.find(l);
    kassert(i != m_loggers.end());
    m_loggers.erase(i);
}

void PerfCountersCollectionImpl::clear() {
    perf_counters_set_t::iterator i = m_loggers.begin();
    perf_counters_set_t::iterator i_end = m_loggers.end();
    for (; i != i_end;) {
        delete *i;
        m_loggers.erase(i++);
    }

    by_path.clear();
}

bool PerfCountersCollectionImpl::reset(const std::string &name) {
    bool result = false;
    perf_counters_set_t::iterator i = m_loggers.begin();
    perf_counters_set_t::iterator i_end = m_loggers.end();

    if (!strcmp(name.c_str(), "all")) {
        while (i != i_end) {
            (*i)->reset();
            ++i;
        }
        result = true;
    } else {
        while (i != i_end) {
            if (!name.compare((*i)->get_name())) {
                (*i)->reset();
                result = true;
                break;
            }
            ++i;
        }
    }

    return result;
}

void PerfCountersCollectionImpl::with_counters(std::function<void(const PerfCountersCollectionImpl::CounterMap &)> fn) const { fn(by_path); }

// ---------------------------

PerfCounters::~PerfCounters() {}

void PerfCounters::inc(int idx, uint64_t amt) {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_U64)) return;
    if (data.type & PERFCOUNTER_LONGRUNAVG) {
        data.avgcount++;
        data.u64 += amt;
        data.avgcount2++;
    } else {
        data.u64 += amt;
    }
}

void PerfCounters::dec(int idx, uint64_t amt) {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    kassert(!(data.type & PERFCOUNTER_LONGRUNAVG));
    if (!(data.type & PERFCOUNTER_U64)) return;
    data.u64 -= amt;
}

void PerfCounters::set(int idx, uint64_t amt) {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_U64)) return;

    if (data.type & PERFCOUNTER_LONGRUNAVG) {
        data.avgcount++;
        data.u64 = amt;
        data.avgcount2++;
    } else {
        data.u64 = amt;
    }
}

uint64_t PerfCounters::get(int idx) const {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    const perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_U64)) return 0;
    return data.u64;
}

void PerfCounters::tinc(int idx, utime_t amt) {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_TIME)) return;
    if (data.type & PERFCOUNTER_LONGRUNAVG) {
        data.avgcount++;
        data.u64 += amt.to_nsec();
        data.avgcount2++;
    } else {
        data.u64 += amt.to_nsec();
    }
}

void PerfCounters::tinc(int idx, common::timespan amt) {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_TIME)) return;
    if (data.type & PERFCOUNTER_LONGRUNAVG) {
        data.avgcount++;
        data.u64 += amt.count();
        data.avgcount2++;
    } else {
        data.u64 += amt.count();
    }
}

void PerfCounters::tset(int idx, utime_t amt) {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_TIME)) return;
    data.u64 = amt.to_nsec();
    if (data.type & PERFCOUNTER_LONGRUNAVG) abort();
}

utime_t PerfCounters::tget(int idx) const {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    const perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_TIME)) return utime_t();
    uint64_t v = data.u64;
    return utime_t(v / 1000000000ull, v % 1000000000ull);
}

void PerfCounters::hinc(int idx, int64_t x, int64_t y) {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);

    perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    kassert(data.type == (PERFCOUNTER_HISTOGRAM | PERFCOUNTER_COUNTER | PERFCOUNTER_U64));
    kassert(data.histogram);

    data.histogram->inc(x, y);
}

pair<uint64_t, uint64_t> PerfCounters::get_tavg_ns(int idx) const {
    kassert(idx > m_lower_bound);
    kassert(idx < m_upper_bound);
    const perf_counter_data_any_d &data(m_data[idx - m_lower_bound - 1]);
    if (!(data.type & PERFCOUNTER_TIME)) return make_pair(0, 0);
    if (!(data.type & PERFCOUNTER_LONGRUNAVG)) return make_pair(0, 0);
    pair<uint64_t, uint64_t> a = data.read_avg();
    return make_pair(a.second, a.first);
}

void PerfCounters::reset() {
    perf_counter_data_vec_t::iterator d = m_data.begin();
    perf_counter_data_vec_t::iterator d_end = m_data.end();

    while (d != d_end) {
        d->reset();
        ++d;
    }
}

const std::string &PerfCounters::get_name() const { return m_name; }

PerfCounters::PerfCounters(Context *context, const std::string &name, int lower_bound, int upper_bound)
    : m_context(context), m_lower_bound(lower_bound), m_upper_bound(upper_bound), m_name(name) {
    m_data.resize(upper_bound - lower_bound - 1);
}

PerfCountersBuilder::PerfCountersBuilder(Context *context, const std::string &name, int first, int last)
    : m_perf_counters(new PerfCounters(context, name, first, last)) {}

PerfCountersBuilder::~PerfCountersBuilder() {
    if (m_perf_counters) delete m_perf_counters;
    m_perf_counters = NULL;
}

void PerfCountersBuilder::add_u64_counter(int idx, const char *name, const char *description, const char *nick, int prio, int unit) {
    add_impl(idx, name, description, nick, prio, PERFCOUNTER_U64 | PERFCOUNTER_COUNTER, unit);
}

void PerfCountersBuilder::add_u64(int idx, const char *name, const char *description, const char *nick, int prio, int unit) {
    add_impl(idx, name, description, nick, prio, PERFCOUNTER_U64, unit);
}

void PerfCountersBuilder::add_u64_avg(int idx, const char *name, const char *description, const char *nick, int prio, int unit) {
    add_impl(idx, name, description, nick, prio, PERFCOUNTER_U64 | PERFCOUNTER_LONGRUNAVG, unit);
}

void PerfCountersBuilder::add_time(int idx, const char *name, const char *description, const char *nick, int prio) {
    add_impl(idx, name, description, nick, prio, PERFCOUNTER_TIME);
}

void PerfCountersBuilder::add_time_avg(int idx, const char *name, const char *description, const char *nick, int prio) {
    add_impl(idx, name, description, nick, prio, PERFCOUNTER_TIME | PERFCOUNTER_LONGRUNAVG);
}

void PerfCountersBuilder::add_u64_counter_histogram(int idx, const char *name, PerfHistogramCommon::axism_rdma_config_ig_d x_axism_rdma_config_ig,
                                                    PerfHistogramCommon::axism_rdma_config_ig_d y_axism_rdma_config_ig, const char *description,
                                                    const char *nick, int prio, int unit) {
    add_impl(idx, name, description, nick, prio, PERFCOUNTER_U64 | PERFCOUNTER_HISTOGRAM | PERFCOUNTER_COUNTER, unit,
             std::unique_ptr<PerfHistogram<>>{new PerfHistogram<>{x_axism_rdma_config_ig, y_axism_rdma_config_ig}});
}

void PerfCountersBuilder::add_impl(int idx, const char *name, const char *description, const char *nick, int prio, int ty, int unit,
                                   std::unique_ptr<PerfHistogram<>> histogram) {
    kassert(idx > m_perf_counters->m_lower_bound);
    kassert(idx < m_perf_counters->m_upper_bound);
    PerfCounters::perf_counter_data_vec_t &vec(m_perf_counters->m_data);
    PerfCounters::perf_counter_data_any_d &data(vec[idx - m_perf_counters->m_lower_bound - 1]);
    kassert(data.type == PERFCOUNTER_NONE);
    data.name = name;
    data.description = description;
    // nick must be <= 4 chars
    if (nick) {
        kassert(strlen(nick) <= 4);
    }
    data.nick = nick;
    data.prio = prio ? prio : prio_default;
    data.type = (enum perfcounter_type_d)ty;
    data.unit = static_cast<unit_t>(unit);
    data.histogram = std::move(histogram);
}

PerfCounters *PerfCountersBuilder::create_perf_counters() {
    PerfCounters::perf_counter_data_vec_t::const_iterator d = m_perf_counters->m_data.begin();
    PerfCounters::perf_counter_data_vec_t::const_iterator d_end = m_perf_counters->m_data.end();
    for (; d != d_end; ++d) {
        kassert(d->type != PERFCOUNTER_NONE);
        kassert(d->type & (PERFCOUNTER_U64 | PERFCOUNTER_TIME));
    }

    PerfCounters *ret = m_perf_counters;
    m_perf_counters = NULL;
    return ret;
}

}  // namespace common::PerfCounter
