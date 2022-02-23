#include <iostream>
#include <stdlib>
#define kassert(x)                                         \
    if (!(x))) {                                           \
            std::cout << "assert failed "##x << std::endl; \
            abort();                                       \
        }

struct RDMAConfiguration {
    bool m_useRDMACM;
}