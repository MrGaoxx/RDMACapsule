// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2017 OVH
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef COMMON_PERF_HISTOGRAM_H
#define COMMON_PERF_HISTOGRAM_H

#include <stdint.h>

#include <array>
#include <atomic>
#include <memory>

#include "common/common.h"
#include "common/formatter.h"

class PerfHistogramCommon {
   public:
    enum class scale_type_d {
        SCALE_LINEAR = 1,
        SCALE_LOG2 = 2,
    };

    struct axism_rdma_config_ig_d {
        const char *m_name = nullptr;
        scale_type_d m_scale_type = scale_type_d::SCALE_LINEAR;
        int64_t m_min = 0;
        int64_t m_quant_size = 0;
        int32_t m_buckets = 0;
        axism_rdma_config_ig_d() = default;
        axism_rdma_config_ig_d(const char *name, scale_type_d scale_type, int64_t min, int64_t quant_size, int32_t buckets)
            : m_name(name), m_scale_type(scale_type), m_min(min), m_quant_size(quant_size), m_buckets(buckets) {}
    };

   protected:
    /// Quantize given value and convert to bucket number on given axis
    static int64_t get_bucket_for_axis(int64_t value, const axism_rdma_config_ig_d &ac);

    /// Calculate inclusive ranges of axis values for each bucket on that axis
    static std::vector<std::pair<int64_t, int64_t>> get_axis_bucket_ranges(const axism_rdma_config_ig_d &ac);
};

/// PerfHistogram does trace a histogram of input values. It's an extended
/// version of a standard histogram which does trace characteristics of a single
/// one value only. In this implementation, values can be traced in multiple
/// dimensions - i.e. we can create a histogram of input request size (first
/// dimension) and processing latency (second dimension). Creating standard
/// histogram out of such multidimensional one is trivial and requires summing
/// values across dimensions we're not interested in.
template <int DIM = 2>
class PerfHistogram : public PerfHistogramCommon {
   public:
    /// Initialize new histogram object
    PerfHistogram(std::initializer_list<axism_rdma_config_ig_d> axesm_rdma_config_ig) {
        kassert(axesm_rdma_config_ig.size() == DIM);

        int i = 0;
        for (const auto &ac : axesm_rdma_config_ig) {
            kassert(ac.m_buckets > 0);
            kassert(ac.m_quant_size > 0);

            m_axesm_rdma_config_ig[i++] = ac;
        }

        m_rawData.reset(new std::atomic<uint64_t>[get_raw_size()] {});
    }

    /// Copy from other histogram object
    PerfHistogram(const PerfHistogram &other) : m_axesm_rdma_config_ig(other.m_axesm_rdma_config_ig) {
        int64_t size = get_raw_size();
        m_rawData.reset(new std::atomic<uint64_t>[size] {});
        for (int64_t i = 0; i < size; i++) {
            m_rawData[i] = other.m_rawData[i].load();
        }
    }

    /// Set all histogram values to 0
    void reset() {
        auto size = get_raw_size();
        for (auto i = size; --i >= 0;) {
            m_rawData[i] = 0;
        }
    }

    /// Increase counter for given axis values by one
    template <typename... T>
    void inc(T... axis) {
        auto index = get_raw_index_for_value(axis...);
        m_rawData[index]++;
    }

    /// Increase counter for given axis buckets by one
    template <typename... T>
    void inc_bucket(T... bucket) {
        auto index = get_raw_index_for_bucket(bucket...);
        m_rawData[index]++;
    }

    /// Read value from given bucket
    template <typename... T>
    uint64_t read_bucket(T... bucket) const {
        auto index = get_raw_index_for_bucket(bucket...);
        return m_rawData[index];
    }

   protected:
    /// Raw data stored as linear space, internal indexes are calculated on
    /// demand.
    std::unique_ptr<std::atomic<uint64_t>[]> m_rawData;

    /// Configuration of axes
    std::array<axism_rdma_config_ig_d, DIM> m_axesm_rdma_config_ig;

    /// Get number of all histogram counters
    int64_t get_raw_size() {
        int64_t ret = 1;
        for (const auto &ac : m_axesm_rdma_config_ig) {
            ret *= ac.m_buckets;
        }
        return ret;
    }

    /// Calculate m_rawData index from axis values
    template <typename... T>
    int64_t get_raw_index_for_value(T... axes) const {
        static_assert(sizeof...(T) == DIM, "Incorrect number of arguments");
        return get_raw_index_internal<0>(get_bucket_for_axis, 0, axes...);
    }

    /// Calculate m_rawData index from axis bucket numbers
    template <typename... T>
    int64_t get_raw_index_for_bucket(T... buckets) const {
        static_assert(sizeof...(T) == DIM, "Incorrect number of arguments");
        return get_raw_index_internal<0>(
            [](int64_t bucket, const axism_rdma_config_ig_d &ac) {
                kassert(bucket >= 0);
                kassertf(bucket < ac.m_buckets);
                return bucket;
            },
            0, buckets...);
    }

    template <int level = 0, typename F, typename... T>
    int64_t get_raw_index_internal(F bucket_evaluator, int64_t startIndex, int64_t value, T... tail) const {
        static_assert(level + 1 + sizeof...(T) == DIM, "Internal consistency check");
        auto &ac = m_axesm_rdma_config_ig[level];
        auto bucket = bucket_evaluator(value, ac);
        return get_raw_index_internal<level + 1>(bucket_evaluator, ac.m_buckets * startIndex + bucket, tail...);
    }

    template <int level, typename F>
    int64_t get_raw_index_internal(F, int64_t startIndex) const {
        static_assert(level == DIM, "Internal consistency check");
        return startIndex;
    }

    /// Visit all histogram counters, call onDimensionEnter / onDimensionLeave
    /// when starting / finishing traversal
    /// on given axis, call onValue when dumping raw histogram counter value.
    template <typename FDE, typename FV, typename FDL>
    void visit_values(FDE onDimensionEnter, FV onValue, FDL onDimensionLeave, int level = 0, int startIndex = 0) const {
        if (level == DIM) {
            onValue(m_rawData[startIndex]);
            return;
        }

        onDimensionEnter(level);
        auto &ac = m_axesm_rdma_config_ig[level];
        startIndex *= ac.m_buckets;
        for (int32_t i = 0; i < ac.m_buckets; ++i, ++startIndex) {
            visit_values(onDimensionEnter, onValue, onDimensionLeave, level + 1, startIndex);
        }
        onDimensionLeave(level);
    }
};

#endif
