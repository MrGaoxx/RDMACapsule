#ifndef COMMON_CONTEXT
#define COMMON_CONTEXT

#include <cstdint>
#include <string>

#include "common/perf_counters_collection.h"
#include "network/Event.h"

struct RDMAConfig;
namespace common::PerfCounter {
class PerfCountersCollection;
}
struct Context {
    Context(std::string rdma_device_name, uint8_t gid_index, uint8_t rdma_port_num)
        : m_rdma_config_(new RDMAConfig(rdma_device_name, gid_index, rdma_port_num)), m_counter_collection_(this), m_associateCenters(){};
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
    RDMAConfig(std::string rdma_device_name, uint8_t gid_index, uint8_t rdma_port_num)
        : m_rdma_device_name_(rdma_device_name), m_gid_index_(gid_index), m_rdma_port_num_(rdma_port_num){};

    bool m_use_rdma_cm_ = true;
    bool m_rdma_enable_hugepage_ = false;
    bool m_rdma_support_srq_ = false;
    std::string m_rdma_device_name_;

    uint8_t m_gid_index_;
    uint8_t m_rdma_dscp_ = 0;
    uint8_t m_rdma_sl_ = 0;
    uint8_t m_rdma_port_num_;

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
#ifdef HAVE_MULTICAST
    MulticastManagementConnectGroup*;
#endif
};
#endif
