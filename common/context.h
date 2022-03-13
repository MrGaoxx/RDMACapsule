#ifndef COMMON_CONTEXT
#define COMMON_CONTEXT

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>
#include <string>

#include "common/perf_counters_collection.h"
#include "network/Event.h"

struct RDMAConfig;
namespace common::PerfCounter {
class PerfCountersCollection;
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
    uint8_t m_rdma_port_num_ = 0;

    uint32_t m_rdma_buffer_size_bytes_ = 4294967296;
    uint32_t m_rdma_send_queeu_len_ = 128;
    uint32_t m_rdma_receive_buffers_bytes_ = 65536;
    uint32_t m_rdma_receive_queue_len_ = 128;
    uint32_t m_rdma_polling_us_ = 1;

    bool m_tcp_nodelay_ = true;
    uint32_t m_tcp_listen_backlog_ = 128;
    bool m_bind_before_connect_ = true;
    uint32_t m_tcp_rcvbuf_bytes_ = 65536;

    uint16_t m_op_threads_num_ = 32;
    std::string m_ipAddr;
#ifdef HAVE_MULTICAST
    MulticastManagementConnectGroup*;
#endif
   private:
    void parse(std::string& key, std::string& val);
};

RDMAConfig::RDMAConfig(std::string& filename) {
    std::fstream configFile;
    configFile.open(filename, std::ios_base::app | std::ios_base::in);
    if (!configFile.is_open()) {
        std::cout << " can not open configuration file " << std::endl;
        abort();
    }

    std::string key, val;
    while (!configFile.eof()) {
        std::cin >> key >> val;
        parse(key, val);
    }
    configFile.close();
}

inline void RDMAConfig::parse(std::string& key, std::string& val) {
    if (key.compare("RDMA_CM")) {
        if (val.compare("true")) {
            m_use_rdma_cm_ = true;
        }
    } else if (key.compare("RDMA_ENABLE_HUGE_PAGE")) {
        if (val.compare("true")) {
            m_rdma_enable_hugepage_ = true;
        }
    } else if (key.compare("RDMA_SURRPORT_SRQ")) {
        if (val.compare("true")) {
            m_rdma_support_srq_ = true;
        }
    }  // TO DO: finish all other configurations
    else if (key.compare("IP")) {
        m_ipAddr = val;
    } else if (key.compare("RDMA_DEVICE_NAME")) {
        m_rdma_device_name_ = val;
    }
    return;
}

#endif
