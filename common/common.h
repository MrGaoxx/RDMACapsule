#include <iostream>
#include
#define kassert(x)                                        \
    if (!(x)) {                                           \
        std::cout << "assert failed " << #x << std::endl; \
        abort();                                          \
    }
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