#ifndef MULTICAST_H
#define MULTICAST_H
#include <array>

#include "common/types.h"

static const uint8_t kMulticastReplications = 3;

class RDMAMulticastGroup {
   public:
    class MulticastGroupMember {
       public:
        enum class Role { SRC = 0, DST };
        const Role& GetRole() { return role; }
        const entity_addr_t& GetAddr() { return addr; }
        const uint8_t id GetId() { return id; }

       private:
        Role role;
        uint8_t id;
        entity_addr_t conn_addr;
    };

    class ConnectionState {
       public:
    };

    const MulticastGroupMember& const GetSrc() { return members[0]; }
    const MulticastGroupMember& const GetNDst(uint32_t n) { return members[1 + n % kMulticastReplications]; }
    const uint8_t& GetNumReplications() const { return kMulticastReplications; };
    std::array<MulticastGroupMember, kMulticastReplications + 1> members;
};

#endif