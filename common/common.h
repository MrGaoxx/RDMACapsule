#include <iostream>

#define kassert(x)                                        \
    if (!(x)) {                                           \
        std::cout << "assert failed " << #x << std::endl; \
        abort();                                          \
    }

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
};

std::string cpp_strerror(int err) {
    char buf[128];
    char *errmsg;

    if (err < 0) err = -err;
    std::ostringstream oss;

    if (errmsg = strerror_r(err, buf, sizeof(buf))) {
        errmsg = "Unknown error %d";
    }
    oss << "(" << err << ") " << errmsg;

    return oss.str();
}