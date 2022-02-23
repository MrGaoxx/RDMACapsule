#include <iostream>
#include <stdlib>
#define kassert(x)                                         \
    if (!(x))) {                                           \
            std::cout << "assert failed "##x << std::endl; \
            abort();                                       \
        }

struct Configure {
    RDMAConfiguration m_rdma_config_;
}

struct RDMAConfiguration {
    bool m_use_rdma_cm;
}