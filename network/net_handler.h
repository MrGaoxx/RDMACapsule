// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 UnitedStack <haomai@unitedstack.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef NEYWORK_NET_HANDLER_H
#define NETWORK_NET_HANDLER_H
#include "common/common.h"
#include "common/context.h"
#include "common/types.h"
namespace Network {
class NetHandler {
   public:
    explicit NetHandler() = delete;

    static int create_socket(int domain, bool reuse_addr = false);

    static int generic_connect(Context *cct, const entity_addr_t &addr, const entity_addr_t &bind_addr, bool nonblock);
    static int connect(Context *cct, const entity_addr_t &addr, const entity_addr_t &bind_addr);
    static int reconnect(const entity_addr_t &addr, int sd);
    static int nonblock_connect(Context *cct, const entity_addr_t &addr, const entity_addr_t &bind_addr);

    static int set_nonblock(int sd);
    static int set_socket_options(int sd, bool nodelay, int size);
    static void set_priority(int sd, int priority, int domain);

    /**
     * Try to reconnect the socket.
     *
     * @return    0         success
     *            > 0       just break, and wait for event
     *            < 0       need to goto fail
     */
};
}  // namespace Network

#endif
