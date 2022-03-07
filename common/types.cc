// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "types.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

std::ostream &operator<<(std::ostream &out, const entity_addr_t &addr) {
    if (addr.type != entity_addr_t::type_t::TYPE_ANY) {
        out << entity_addr_t::get_type_name(addr.type) << ":";
    }
    out << addr.get_sockaddr() << '/' << addr.nonce;
    return out;
}

std::ostream &operator<<(std::ostream &out, const sockaddr *psa) {
    char buf[NI_MAXHOST] = {0};

    switch (psa->sa_family) {
        case AF_INET: {
            const sockaddr_in *sa = (const sockaddr_in *)psa;
            inet_ntop(AF_INET, &sa->sin_addr, buf, NI_MAXHOST);
            return out << buf << ':' << ntohs(sa->sin_port);
        }
        case AF_INET6: {
            const sockaddr_in6 *sa = (const sockaddr_in6 *)psa;
            inet_ntop(AF_INET6, &sa->sin6_addr, buf, NI_MAXHOST);
            return out << '[' << buf << "]:" << ntohs(sa->sin6_port);
        }
        default:
            return out << "(unrecognized address family " << psa->sa_family << ")";
    }
}

std::string entity_addr_t::ip_only_to_str() const {
    const char *host_ip = NULL;
    char addr_buf[INET6_ADDRSTRLEN];
    switch (get_family()) {
        case AF_INET:
            host_ip = inet_ntop(AF_INET, &in4_addr().sin_addr, addr_buf, INET_ADDRSTRLEN);
            break;
        case AF_INET6:
            host_ip = inet_ntop(AF_INET6, &in6_addr().sin6_addr, addr_buf, INET6_ADDRSTRLEN);
            break;
        default:
            break;
    }
    return host_ip ? host_ip : "";
}
