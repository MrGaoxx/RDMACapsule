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

#include "PosixStack.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

#include "common/buffer.h"
#include "network/net_handler.h"

constexpr uint32_t kPosixIOVMax = 1024;
class PosixConnectedSocketImpl final : public ConnectedSocketImpl {
    int _fd;
    entity_addr_t sa;
    bool connected;

   public:
    explicit PosixConnectedSocketImpl(const entity_addr_t &sa, int f, bool connected) : _fd(f), sa(sa), connected(connected) {}

    int is_connected() override {
        if (connected) return 1;

        int r = Network::NetHandler::reconnect(sa, _fd);
        if (r == 0) {
            connected = true;
            return 1;
        } else if (r < 0) {
            return r;
        } else {
            return 0;
        }
    }

    ssize_t read(char *buf, size_t len) override {
        ssize_t r = ::read(_fd, buf, len);
        if (r < 0) r = -errno;
        return r;
    }

    // return the sent length
    // < 0 means error occurred

    static ssize_t do_sendmsg(int fd, struct msghdr &msg, unsigned len, bool more) {
        size_t sent = 0;
        while (1) {
            ssize_t r;
            r = ::sendmsg(fd, &msg, MSG_NOSIGNAL | (more ? MSG_MORE : 0));
            if (r < 0) {
                int err = errno;
                if (err == EINTR) {
                    continue;
                } else if (err == EAGAIN) {
                    break;
                }
                return -err;
            }

            sent += r;
            if (len == sent) break;

            while (r > 0) {
                if (msg.msg_iov[0].iov_len <= (size_t)r) {
                    // drain this whole item
                    r -= msg.msg_iov[0].iov_len;
                    msg.msg_iov++;
                    msg.msg_iovlen--;
                } else {
                    msg.msg_iov[0].iov_base = (char *)msg.msg_iov[0].iov_base + r;
                    msg.msg_iov[0].iov_len -= r;
                    break;
                }
            }
        }
        return (ssize_t)sent;
    }

    ssize_t send(BufferList &bl, bool more) override {
        size_t sent_bytes = 0;
        auto pb = bl.get_begin();
        uint64_t left_pbrs = bl.GetSize();
        while (left_pbrs) {
            struct msghdr msg;
            struct iovec msgvec[kPosixIOVMax];
            uint64_t size = std::min<uint64_t>(left_pbrs, kPosixIOVMax);
            left_pbrs -= size;
            // FIPS zeroization audit 20191115: this memset is not security related.
            memset(&msg, 0, sizeof(msg));
            msg.msg_iovlen = size;
            msg.msg_iov = msgvec;
            unsigned msglen = 0;
            for (auto iov = msgvec; iov != msgvec + size; iov++) {
                iov->iov_base = (void *)(pb->get_buffer());
                iov->iov_len = pb->get_len();
                msglen += iov->iov_len;
                ++pb;
            }
            ssize_t r = do_sendmsg(_fd, msg, msglen, left_pbrs || more);
            if (r < 0) return r;

            // "r" is the remaining length
            sent_bytes += r;
            if (static_cast<unsigned>(r) < msglen) break;
            // only "r" == 0 continue
        }

        if (sent_bytes) {
            if (sent_bytes < bl.get_len()) {
                bl.Move(sent_bytes);
            } else {
                bl.Clear();
            }
        }

        return static_cast<ssize_t>(sent_bytes);
    }
    void shutdown() override { ::shutdown(_fd, SHUT_RDWR); }
    void close() override { ::close(_fd); }
    int fd() const override { return _fd; }
    friend class PosixServerSocketImpl;
    friend class PosixNetworkStack;
};

class PosixServerSocketImpl : public ServerSocketImpl {
    int _fd;

   public:
    explicit PosixServerSocketImpl(int f, const entity_addr_t &listen_addr) : ServerSocketImpl(listen_addr.get_type()), _fd(f) {}
    int accept(ConnectedSocket *sock, const SocketOptions &opts, entity_addr_t *out, Worker *w) override;
    void abort_accept() override {
        ::close(_fd);
        _fd = -1;
    }
    int fd() const override { return _fd; }
};

int PosixServerSocketImpl::accept(ConnectedSocket *sock, const SocketOptions &opt, entity_addr_t *out, Worker *w) {
    kassert(sock);
    sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int sd = ::accept(_fd, (sockaddr *)&ss, &slen);
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

    out->set_type(addr_type);
    out->set_sockaddr((sockaddr *)&ss);
    Network::NetHandler::set_priority(sd, opt.priority, out->get_family());

    std::unique_ptr<PosixConnectedSocketImpl> csi(new PosixConnectedSocketImpl(*out, sd, true));
    *sock = ConnectedSocket(std::move(csi));
    return 0;
}

void PosixWorker::initialize() {}

int PosixWorker::listen(entity_addr_t &sa, const SocketOptions &opt, ServerSocket *sock) {
    int listen_sd = Network::NetHandler::create_socket(sa.get_family(), true);
    if (listen_sd < 0) {
        return -errno;
    }

    int r = Network::NetHandler::set_nonblock(listen_sd);
    if (r < 0) {
        ::close(listen_sd);
        return -errno;
    }

    r = Network::NetHandler::set_socket_options(listen_sd, opt.nodelay, opt.rcbuf_size);
    if (r < 0) {
        ::close(listen_sd);
        return -errno;
    }

    r = ::bind(listen_sd, sa.get_sockaddr(), sa.get_sockaddr_len());
    if (r < 0) {
        r = -errno;
        std::cout << __func__ << " unable to bind to " << sa.get_sockaddr() << ": " << cpp_strerror(r) << std::endl;
        ::close(listen_sd);
        return r;
    }

    r = ::listen(listen_sd, context->m_rdma_config_->m_tcp_listen_backlog_);
    if (r < 0) {
        r = -errno;
        std::cout << __func__ << " unable to listen on " << sa << ": " << cpp_strerror(r) << std::endl;
        ::close(listen_sd);
        return r;
    }

    *sock = ServerSocket(std::unique_ptr<PosixServerSocketImpl>(new PosixServerSocketImpl(listen_sd, sa)));
    return 0;
}

int PosixWorker::connect(const entity_addr_t &addr, const SocketOptions &opts, ConnectedSocket *socket) {
    int sd;

    if (opts.nonblock) {
        sd = Network::NetHandler::nonblock_connect(context, addr, opts.connect_bind_addr);
    } else {
        sd = Network::NetHandler::connect(context, addr, opts.connect_bind_addr);
    }

    if (sd < 0) {
        return -errno;
    }

    Network::NetHandler::set_priority(sd, opts.priority, addr.get_family());
    *socket = ConnectedSocket(std::unique_ptr<PosixConnectedSocketImpl>(new PosixConnectedSocketImpl(addr, sd, !opts.nonblock)));
    return 0;
}

PosixNetworkStack::PosixNetworkStack(Context *c) : NetworkStack(c) {}