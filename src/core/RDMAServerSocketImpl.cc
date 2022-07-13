// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2016 XSKY <haomai@xsky.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "RDMAStack.h"
#include "network/net_handler.h"

RDMAServerSocketImpl::RDMAServerSocketImpl(Context *context, std::shared_ptr<Infiniband> &ib, std::shared_ptr<RDMADispatcher> &rdma_dispatcher,
                                           RDMAWorker *w, entity_addr_t &a)
    : ServerSocketImpl(a), context(context), server_setup_socket(-1), ib(ib), dispatcher(rdma_dispatcher), worker(w), sa(a) {}

int RDMAServerSocketImpl::listen(entity_addr_t &sa, const SocketOptions &opt) {
    int rc = 0;
    server_setup_socket = Network::NetHandler::create_socket(sa.get_family(), true);
    if (server_setup_socket < 0) {
        rc = -errno;
        std::cout << typeid(this).name() << " : " << __func__ << " failed to create server socket: " << cpp_strerror(errno) << std::endl;
        return rc;
    }

    rc = Network::NetHandler::set_nonblock(server_setup_socket);
    if (rc < 0) {
        goto err;
    }

    rc = Network::NetHandler::set_socket_options(server_setup_socket, opt.nodelay, opt.rcbuf_size);
    if (rc < 0) {
        goto err;
    }

    rc = ::bind(server_setup_socket, sa.get_sockaddr(), sa.get_sockaddr_len());
    if (rc < 0) {
        rc = -errno;
        std::cout << typeid(this).name() << " : " << __func__ << " unable to bind to " << sa.get_sockaddr() << " on port " << sa.get_port() << ": "
                  << cpp_strerror(errno) << std::endl;
        goto err;
    }

    rc = ::listen(server_setup_socket, context->m_rdma_config_->m_tcp_listen_backlog_);
    if (rc < 0) {
        rc = -errno;
        std::cout << typeid(this).name() << " : " << __func__ << " unable to listen on " << sa << ": " << cpp_strerror(errno) << std::endl;
        goto err;
    }

    std::cout << typeid(this).name() << " : " << __func__ << " bind to " << sa.get_sockaddr() << " on port " << sa.get_port() << std::endl;
    return 0;

err:
    ::close(server_setup_socket);
    server_setup_socket = -1;
    return rc;
}

int RDMAServerSocketImpl::accept(ConnectedSocket *sock, const SocketOptions &opt, entity_addr_t *out, Worker *w) {
    // std::cout << typeid(this).name() << " : " << __func__ << std::endl;

    kassert(sock);

    sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int sd = ::accept(server_setup_socket, (sockaddr *)&ss, &slen);
    if (sd < 0) {
        return -errno;
    }

    int r = Network::NetHandler::set_nonblock(sd);
    if (r < 0) {
        ::close(sd);
        return -errno;
    }

    r = Network::NetHandler::set_socket_options(sd, opt.nodelay, opt.rcbuf_size);
    if (r < 0) {
        ::close(sd);
        return -errno;
    }

    kassert(NULL != out);  // out should not be NULL in accept connection

    out->set_type(addr.type);
    out->set_sockaddr((sockaddr *)&ss);
    Network::NetHandler::set_priority(sd, opt.priority, out->get_family());

    RDMAConnectedSocketImpl *server;
    // Worker* w = dispatcher->get_stack()->get_worker();
    server = new RDMAConnectedSocketImpl(context, ib, dispatcher, dynamic_cast<RDMAWorker *>(w));
    if (!server->get_qp()) {
        std::cout << typeid(this).name() << " : " << __func__ << " server->qp is null" << std::endl;
        // cann't use delete server here, destructor will fail.
        server->cleanup();
        ::close(sd);
        return -1;
    }
    server->set_accept_fd(sd);
    std::cout << typeid(this).name() << " : " << __func__ << " accepted a new QP, tcp_fd: " << sd << std::endl;
    std::unique_ptr<RDMAConnectedSocketImpl> csi(server);
    *sock = ConnectedSocket(std::move(csi));

    return 0;
}

void RDMAServerSocketImpl::abort_accept() {
    if (server_setup_socket >= 0) ::close(server_setup_socket);
}
