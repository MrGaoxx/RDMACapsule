#ifndef COMMON_CONTEXT
#define COMMON_CONTEXT

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>
#include <string>

#include "common/perf_counters_collection.h"
#include "network/Event.h"

namespace common::PerfCounter {
class PerfCountersCollection;
}

#ifdef HAVE_MULTICAST
class MulticastConnectGroup;
#endif

struct RDMAConfig {
    RDMAConfig(std::string& filename);
    RDMAConfig(std::string& rdma_device_name, uint8_t gid_index, uint8_t rdma_port_num)
        : m_rdma_device_name_(rdma_device_name), m_gid_index_(gid_index), m_rdma_port_num_(rdma_port_num){};

    bool m_use_rdma_cm_ = false;
    bool m_rdma_enable_hugepage_ = false;
    bool m_rdma_support_srq_ = false;
    std::string m_rdma_device_name_;

    uint8_t m_gid_index_ = 3;
    uint8_t m_rdma_dscp_ = 0;
    uint8_t m_rdma_sl_ = 0;
    uint8_t m_rdma_port_num_ = 1;

    uint32_t m_rdma_buffer_size_bytes_ = 32 * 1024;
    uint32_t m_rdma_send_queeu_len_ = 128;
    uint32_t m_rdma_receive_buffers_bytes_ = 64 * 1024;
    uint32_t m_rdma_receive_queue_len_ = 128;
    uint32_t m_rdma_polling_us_ = 1;

    uint8_t m_bind_retry_count_ = 1 << 3;
    uint32_t m_bind_retry_delay_seconds_ = 1;  // seconds
    uint16_t m_bind_port_min_;
    uint16_t m_bind_port_max_;

    bool m_tcp_nodelay_ = true;
    uint8_t m_tcp_priority_ = 0;
    uint32_t m_tcp_rcvbuf_ = 64 * 1024;
    uint32_t m_tcp_listen_backlog_ = 128;
    bool m_bind_before_connect_ = true;
    uint32_t m_tcp_rcvbuf_bytes_ = 64 * 1024;
    uint8_t m_max_accept_failures_ = 64;

    uint16_t m_op_threads_num_ = 32;
    std::string m_ipAddr;
#ifdef HAVE_MULTICAST
    MulticastManagementConnectGroup*;
#endif
   private:
    void parse(std::string& key, std::string& val);
};

inline RDMAConfig::RDMAConfig(std::string& filename) {
    std::fstream configFile;
    configFile.open(filename, std::ios_base::app | std::ios_base::in);
    if (!configFile.is_open()) {
        std::cout << " can not open configuration file " << std::endl;
        abort();
    }

    std::string key, val;
    while (!configFile.eof()) {
        configFile >> key >> val;
        parse(key, val);
    }
    configFile.close();
}

inline void RDMAConfig::parse(std::string& key, std::string& val) {
    if (key == "RDMA_CM") {
        if (val == "true") {
            m_use_rdma_cm_ = true;
        } else {
            m_use_rdma_cm_ = false;
        }
        std::cout << " use_rdma_cm = " << m_use_rdma_cm_ << std::endl;
    } else if (key == "RDMA_ENABLE_HUGE_PAGE") {
        if (val == "true") {
            m_rdma_enable_hugepage_ = true;
        } else {
            m_rdma_enable_hugepage_ = false;
        }
        std::cout << " rdma_enable_hugepage = " << m_rdma_enable_hugepage_ << std::endl;
    } else if (key == "RDMA_SURRPORT_SRQ") {
        if (val == "true") {
            m_rdma_support_srq_ = true;
        } else {
            m_rdma_support_srq_ = false;
        }
        std::cout << " rdma_enable_hugepage = " << m_rdma_enable_hugepage_ << std::endl;
    }  // TO DO: finish all other configurations
    else if (key == "IP") {
        m_ipAddr = val;
        std::cout << " ipAddr = " << m_ipAddr << std::endl;
    } else if (key == "RDMA_DEVICE_NAME") {
        m_rdma_device_name_ = val;
        std::cout << " rdma_device_name = " << m_rdma_device_name_ << std::endl;
    }
    return;
}

struct Context {
    Context(std::string& config_file) : m_rdma_config_(new RDMAConfig(config_file)), m_counter_collection_(this), m_associateCenters(){};
    Context(RDMAConfig* config_) : m_rdma_config_(config_), m_counter_collection_(this){};
    ~Context() { delete m_rdma_config_; }

    common::PerfCounter::PerfCountersCollection* get_perfcounters_collection() { return &m_counter_collection_; }

    RDMAConfig* m_rdma_config_;
    common::PerfCounter::PerfCountersCollection m_counter_collection_;
    EventCenter::AssociatedCenters m_associateCenters;
};

#endif
