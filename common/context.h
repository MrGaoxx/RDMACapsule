#ifndef COMMON_CONTEXT
#define COMMON_CONTEXT

#include <cstdint>
#include <string>

struct RDMAConfig;
struct Context {
    RDMAConfig *m_rdma_config_;
};

struct RDMAConfig {
    bool m_use_rdma_cm_;
    bool m_rdma_enable_hugepage_;
    bool m_rdma_support_srq_;
    std::string m_rdma_device_name_;

    uint8_t m_gid_index_;
    uint8_t m_rdma_dscp_;
    uint8_t m_rdma_sl_;
    uint16_t m_rdma_port_num_;

    uint32_t m_rdma_buffer_size_bytes_;
    uint32_t m_rdma_send_buffers_;
    uint32_t m_rdma_receive_buffers_bytes_;

    uint32_t m_rdma_receive_queue_len_;

    bool m_tcp_nodelay_;
    bool m_tcp_listen_backlog_;
    bool m_bind_before_connect_;
    uint32_t m_tcp_rcvbuf_bytes_;
};
#endif
