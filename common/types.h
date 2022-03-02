// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef NETWORK_TYPES_H
#define NETWORK_TYPES_H

#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cstdint>
#include <sstream>
#include <vector>

#define MAX_PORT_NUMBER 65535

std::ostream& operator<<(std::ostream& out, const sockaddr* sa);

/*
 * an entity's network address.
 * includes a random value that prevents it from being reused.
 * thus identifies a particular process instance.
 * ipv4 for now.
 */
struct entity_addr_t {
    typedef enum {
        TYPE_SERVER = 0,
        TYPE_CLIENT = 1,  ///< legacy msgr1 protocol (ceph jewel and older)
        TYPE_SWITCH = 2,  ///< msgr2 protocol (new in ceph kraken)
        TYPE_ANY = 3,     ///< ambiguous
    } type_t;
    static const type_t TYPE_DEFAULT = TYPE_SERVER;

    uint32_t type;
    uint32_t nonce;
    union {
        sockaddr sa;
        sockaddr_in sin;
        sockaddr_in6 sin6;
    } u;

    static std::string_view get_type_name(int t) {
        switch (t) {
            case TYPE_SERVER:
                return "Server";
            case TYPE_CLIENT:
                return "Client";
            case TYPE_SWITCH:
                return "Switch";
            case TYPE_ANY:
                return "any";
            default:
                return "NOT DEFINDE";
        }
    };

    entity_addr_t() : type(0), nonce(0) { memset(&u, 0, sizeof(u)); }
    entity_addr_t(uint32_t _type, uint32_t _nonce) : type(_type), nonce(_nonce) { memset(&u, 0, sizeof(u)); }
}

uint32_t
get_type() const {
    return type;
}
void set_type(uint32_t t) { type = t; }
bool is_legacy() const { return type == TYPE_LEGACY; }
bool is_msgr2() const { return type == TYPE_MSGR2; }
bool is_any() const { return type == TYPE_ANY; }

uint32_t get_nonce() const { return nonce; }
void set_nonce(uint32_t n) { nonce = n; }

int get_family() const { return u.sa.sa_family; }
void set_family(int f) { u.sa.sa_family = f; }

bool is_ipv4() const { return u.sa.sa_family == AF_INET; }
bool is_ipv6() const { return u.sa.sa_family == AF_INET6; }

sockaddr_in& in4_addr() { return u.sin; }
const sockaddr_in& in4_addr() const { return u.sin; }
sockaddr_in6& in6_addr() { return u.sin6; }
const sockaddr_in6& in6_addr() const { return u.sin6; }
const sockaddr* get_sockaddr() const { return &u.sa; }
size_t get_sockaddr_len() const {
    switch (u.sa.sa_family) {
        case AF_INET:
            return sizeof(u.sin);
        case AF_INET6:
            return sizeof(u.sin6);
    }
    return sizeof(u);
}
bool set_sockaddr(const struct sockaddr* sa) {
    switch (sa->sa_family) {
        case AF_INET:
            // pre-zero, since we're only copying a portion of the source
            memset(&u, 0, sizeof(u));
            memcpy(&u.sin, sa, sizeof(u.sin));
            break;
        case AF_INET6:
            // pre-zero, since we're only copying a portion of the source
            memset(&u, 0, sizeof(u));
            memcpy(&u.sin6, sa, sizeof(u.sin6));
            break;
        case AF_UNSPEC:
            memset(&u, 0, sizeof(u));
            break;
        default:
            return false;
    }
    return true;
}

void set_in4_quad(int pos, int val) {
    u.sin.sin_family = AF_INET;
    unsigned char* ipq = (unsigned char*)&u.sin.sin_addr.s_addr;
    ipq[pos] = val;
}
void set_port(int port) {
    switch (u.sa.sa_family) {
        case AF_INET:
            u.sin.sin_port = htons(port);
            break;
        case AF_INET6:
            u.sin6.sin6_port = htons(port);
            break;
        default:
            ceph_abort();
    }
}
int get_port() const {
    switch (u.sa.sa_family) {
        case AF_INET:
            return ntohs(u.sin.sin_port);
        case AF_INET6:
            return ntohs(u.sin6.sin6_port);
    }
    return 0;
}

bool probably_equals(const entity_addr_t& o) const {
    if (get_port() != o.get_port()) return false;
    if (get_nonce() != o.get_nonce()) return false;
    if (is_blank_ip() || o.is_blank_ip()) return true;
    if (memcmp(&u, &o.u, sizeof(u)) == 0) return true;
    return false;
}

bool is_same_host(const entity_addr_t& o) const {
    if (u.sa.sa_family != o.u.sa.sa_family) return false;
    if (u.sa.sa_family == AF_INET) return u.sin.sin_addr.s_addr == o.u.sin.sin_addr.s_addr;
    if (u.sa.sa_family == AF_INET6) return memcmp(u.sin6.sin6_addr.s6_addr, o.u.sin6.sin6_addr.s6_addr, sizeof(u.sin6.sin6_addr.s6_addr)) == 0;
    return false;
}

bool is_blank_ip() const {
    switch (u.sa.sa_family) {
        case AF_INET:
            return u.sin.sin_addr.s_addr == INADDR_ANY;
        case AF_INET6:
            return memcmp(&u.sin6.sin6_addr, &in6addr_any, sizeof(in6addr_any)) == 0;
        default:
            return true;
    }
}

bool is_ip() const {
    switch (u.sa.sa_family) {
        case AF_INET:
        case AF_INET6:
            return true;
        default:
            return false;
    }
}

std::string ip_only_to_str() const;

std::ostream& operator<<(std::ostream& out, const entity_addr_t& addr);

inline bool operator==(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) == 0; }
inline bool operator!=(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) != 0; }
inline bool operator<(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) < 0; }
inline bool operator<=(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) <= 0; }
inline bool operator>(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) > 0; }
inline bool operator>=(const entity_addr_t& a, const entity_addr_t& b) { return memcmp(&a, &b, sizeof(a)) >= 0; }

#endif
