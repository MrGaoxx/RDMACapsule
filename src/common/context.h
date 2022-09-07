#ifndef COMMON_CONTEXT
#define COMMON_CONTEXT

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>

#include <cerrno>
#include <fstream>
#include <iostream>
#include <string>

#include "common/perf_counters_collection.h"
#include "common/types.h"
#include "network/Event.h"
namespace common::PerfCounter {
class PerfCountersCollection;
}

struct MulticastConnectGroup {
    uint32_t ip_addr_member_1;
    uint16_t port_member_1;
    uint32_t ip_addr_member_2;
    uint16_t port_member_2;
};

struct Config {
    Config(std::string& filename);
    Config(std::string& rdma_device_name, uint8_t gid_index, uint8_t rdma_port_num)
        : m_use_rdma_(true), m_rdma_device_name_(rdma_device_name), m_gid_index_(gid_index), m_rdma_port_num_(rdma_port_num){};

    bool m_use_rdma_ = true;

    bool m_use_rdma_cm_ = false;
    bool m_rdma_enable_hugepage_ = false;
    bool m_rdma_support_srq_ = false;
    std::string m_rdma_device_name_ = "mlx5_0";

    uint8_t m_gid_index_ = 3;
    uint8_t m_rdma_dscp_ = 0;
    uint8_t m_rdma_sl_ = 0;
    uint8_t m_rdma_port_num_ = 1;

    uint32_t m_rdma_buffer_size_bytes_ = 32 * 1024;
    uint32_t m_rdma_send_queeu_len_ = 128;
    uint32_t m_rdma_receive_buffers_bytes_ = 64 * 1024;
    uint32_t m_rdma_receive_queue_len_ = 128;
    uint32_t m_rdma_polling_us_ = 128;

    uint8_t m_bind_retry_count_ = 1 << 3;
    uint32_t m_bind_retry_delay_seconds_ = 1;  // seconds

    bool m_tcp_nodelay_ = true;
    uint8_t m_tcp_priority_ = 0;
    uint32_t m_tcp_rcvbuf_ = 64 * 1024;
    uint32_t m_tcp_listen_backlog_ = 128;
    bool m_bind_before_connect_ = true;
    uint32_t m_tcp_rcvbuf_bytes_ = 64 * 1024;
    uint8_t m_max_accept_failures_ = 64;

    uint16_t m_op_threads_num_ = 1;
    std::string m_ip_addr;
    uint16_t m_listen_port;
    entity_addr_t m_addr;

    MulticastConnectGroup m_mc_group;

    uint32_t m_request_size;
    uint32_t m_request_num;

   private:
    void parse(std::string& key, std::string& val);
};

inline Config::Config(std::string& filename) {
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

inline void Config::parse(std::string& key, std::string& val) {
    if (key == "USE_RDMA") {
        if (val == "true") {
            m_use_rdma_ = true;
        } else {
            m_use_rdma_ = false;
        }
        std::cout << " use_rdma_ = " << m_use_rdma_ << std::endl;
    } else if (key == "RDMA_CM") {
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
        m_ip_addr = val;

        m_addr.nonce = 0;
        m_addr.set_family(AF_INET);
        sockaddr_in* sa = new sockaddr_in;
        inet_pton(AF_INET, m_ip_addr.c_str(), &sa->sin_addr);
        sa->sin_family = AF_INET;
        sa->sin_port = m_addr.u.sin.sin_port;
        m_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(sa));

        std::cout << " ip_addr = " << m_ip_addr << std::endl;
    } else if (key == "LISTEN_PORT") {
        m_listen_port = static_cast<uint16_t>(std::stoi(val));
        m_addr.u.sin.sin_port = htons(m_listen_port);
        std::cout << " listen_port = " << m_listen_port << std::endl;
    } else if (key == "RDMA_DEVICE_NAME") {
        m_rdma_device_name_ = val;
        std::cout << " rdma_device_name = " << m_rdma_device_name_ << std::endl;
    } else if (key == "RDMA_THREADS_NUM") {
        m_op_threads_num_ = std::stoi(val);
        std::cout << " m_op_threads_num = " << m_op_threads_num_ << std::endl;
    } else if (key == "MULTICAST_GROUP_IP1") {
        sockaddr_in sa;
        inet_pton(AF_INET, val.c_str(), &sa.sin_addr);
        m_mc_group.ip_addr_member_1 = sa.sin_addr.s_addr;
        std::cout << " MULTICAST_GROUP_IP1 = " << m_mc_group.ip_addr_member_1 << std::endl;
    } else if (key == "MULTICAST_GROUP_IP2") {
        sockaddr_in sa;
        inet_pton(AF_INET, val.c_str(), &sa.sin_addr);
        m_mc_group.ip_addr_member_2 = sa.sin_addr.s_addr;
        std::cout << " MULTICAST_GROUP_IP2 = " << m_mc_group.ip_addr_member_2 << std::endl;
    } else if (key == "MULTICAST_GROUP_PORT1") {
        m_mc_group.port_member_1 = static_cast<uint16_t>(std::stoi(val));
        std::cout << " MULTICAST_GROUP_PORT1 = " << m_mc_group.port_member_1 << std::endl;
    } else if (key == "MULTICAST_GROUP_PORT2") {
        m_mc_group.port_member_2 = static_cast<uint16_t>(std::stoi(val));
        std::cout << " MULTICAST_GROUP_PORT2 = " << m_mc_group.port_member_2 << std::endl;
    } else if (key == "GID_INDEX") {
        m_gid_index_ = static_cast<uint16_t>(std::stoi(val));
        std::cout << " GID_INDEX = " << m_gid_index_ << std::endl;
    } else if (key == "REQUEST_SIZE") {
        m_request_size = static_cast<uint32_t>(std::stoi(val));
        std::cout << " REQUEST_SIZE = " << m_request_size << std::endl;
    } else if (key == "REQUEST_NUM") {
        m_request_num = static_cast<uint32_t>(std::stoi(val));
        std::cout << " REQUEST_NUM " << m_request_num << std::endl;
    }
    return;
}

struct Context {
    Context(std::string& config_file) : m_rdma_config_(new Config(config_file)), m_counter_collection_(this), m_associateCenters(){};
    Context(Config* config_) : m_rdma_config_(config_), m_counter_collection_(this){};
    ~Context() { delete m_rdma_config_; }

    common::PerfCounter::PerfCountersCollection* get_perfcounters_collection() { return &m_counter_collection_; }

    Config* m_rdma_config_;
    common::PerfCounter::PerfCountersCollection m_counter_collection_;
    EventCenter::AssociatedCenters m_associateCenters;
};

#endif
