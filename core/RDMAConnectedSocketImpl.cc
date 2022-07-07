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

class C_handle_connection_established : public EventCallback {
    RDMAConnectedSocketImpl *csi;
    bool active = true;

   public:
    C_handle_connection_established(RDMAConnectedSocketImpl *w) : csi(w) {}
    void do_request(uint64_t fd) final {
        if (active) csi->handle_connection_established();
    }
    void close() { active = false; }
};

class C_handle_connection_read : public EventCallback {
    RDMAConnectedSocketImpl *csi;
    bool active = true;

   public:
    explicit C_handle_connection_read(RDMAConnectedSocketImpl *w) : csi(w) {}
    void do_request(uint64_t fd) final {
        if (active) csi->handle_connection();
    }
    void close() { active = false; }
};

#undef dout_prefix
#define dout_prefix *_dout << " RDMAConnectedSocketImpl "

RDMAConnectedSocketImpl::RDMAConnectedSocketImpl(Context *context, std::shared_ptr<Infiniband> &ib, std::shared_ptr<RDMADispatcher> &rdma_dispatcher,
                                                 RDMAWorker *w)
    : context(context),
      connected(0),
      error(0),
      ib(ib),
      dispatcher(rdma_dispatcher),
      worker(w),
      is_server(false),
      read_handler(new C_handle_connection_read(this)),
      established_handler(new C_handle_connection_established(this)),
      active(false),
      pending(false) {
    if (!context->m_rdma_config_->m_use_rdma_cm_) {
        qp = ib->create_queue_pair(context, dispatcher->get_tx_cq(), dispatcher->get_rx_cq(), IBV_QPT_RC, NULL);
        if (!qp) {
            std::cout << typeid(this).name() << " : " << __func__ << " queue pair create failed" << std::endl;
            return;
        }
        local_qpn = qp->get_local_qp_number();
        notify_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        dispatcher->register_qp(qp, this);
        dispatcher->perf_logger->inc(l_msgr_rdma_created_queue_pair);
        dispatcher->perf_logger->inc(l_msgr_rdma_active_queue_pair);
    }
}

RDMAConnectedSocketImpl::~RDMAConnectedSocketImpl() {
    std::cout << typeid(this).name() << " : " << __func__ << " destruct." << std::endl;
    cleanup();
    worker->remove_pending_conn(this);
    dispatcher->schedule_qp_destroy(local_qpn);

    for (unsigned i = 0; i < wc.size(); ++i) {
        dispatcher->recall_chunk_to_pool(reinterpret_cast<Chunk *>(wc[i].wr_id));
    }
    for (unsigned i = 0; i < buffers.size(); ++i) {
        dispatcher->recall_chunk_to_pool(buffers[i]);
    }

    std::lock_guard l{lock};
    if (notify_fd >= 0) ::close(notify_fd);
    if (tcp_fd >= 0) ::close(tcp_fd);
    error = ECONNRESET;
}

void RDMAConnectedSocketImpl::pass_wc(std::vector<ibv_wc> &&v) {
    std::lock_guard l{lock};
    if (wc.empty())
        wc = std::move(v);
    else
        wc.insert(wc.end(), v.begin(), v.end());
    notify();
}

void RDMAConnectedSocketImpl::get_wc(std::vector<ibv_wc> &w) {
    std::lock_guard l{lock};
    if (wc.empty()) return;
    w.swap(wc);
}

int RDMAConnectedSocketImpl::activate() {
    qp->get_local_cm_meta().peer_qpn = qp->get_peer_cm_meta().local_qpn;
    if (qp->modify_qp_to_rtr() != 0) return -1;

    if (qp->modify_qp_to_rts() != 0) return -1;

    if (!is_server) {
        connected = 1;  // indicate successfully
        std::cout << typeid(this).name() << " : " << __func__ << " handle fake send, wake it up. QP: " << local_qpn << std::endl;
        submit(false);
    }
    active = true;
    peer_qpn = qp->get_local_cm_meta().peer_qpn;

    return 0;
}

int RDMAConnectedSocketImpl::try_connect(const entity_addr_t &peer_addr, const SocketOptions &opts) {
    std::cout << typeid(this).name() << " : " << __func__ << " nonblock:" << opts.nonblock << ", nodelay:" << opts.nodelay
              << ", rbuf_size: " << opts.rcbuf_size << std::endl;

    // we construct a socket to transport ib sync message
    // but we shouldn't block in tcp connecting
    if (opts.nonblock) {
        tcp_fd = Network::NetHandler::nonblock_connect(context, peer_addr, opts.connect_bind_addr);
    } else {
        tcp_fd = Network::NetHandler::connect(context, peer_addr, opts.connect_bind_addr);
    }

    if (tcp_fd < 0) {
        return -errno;
    }

    int r = Network::NetHandler::set_socket_options(tcp_fd, opts.nodelay, opts.rcbuf_size);
    if (r < 0) {
        ::close(tcp_fd);
        tcp_fd = -1;
        return -errno;
    }

    std::cout << typeid(this).name() << " : " << __func__ << " tcp_fd: " << tcp_fd << std::endl;
    Network::NetHandler::set_priority(tcp_fd, opts.priority, peer_addr.get_family());
    r = 0;
    if (opts.nonblock) {
        worker->center.create_file_event(tcp_fd, EVENT_READABLE | EVENT_WRITABLE, established_handler);
    } else {
        r = handle_connection_established(false);
    }
    return r;
}

int RDMAConnectedSocketImpl::handle_connection_established(bool need_set_fault) {
    std::cout << typeid(this).name() << " : " << __func__ << " start " << std::endl;
    // delete read event
    // worker->center.delete_file_event(tcp_fd, EVENT_READABLE | EVENT_WRITABLE);
    if (1 == connected) {
        std::cout << typeid(this).name() << " : " << __func__ << " warnning: logic failed " << std::endl;
        if (need_set_fault) {
            fault();
        }
        return -1;
    }
    // send handshake msg to server
    qp->get_local_cm_meta().peer_qpn = 0;
    int r = qp->send_cm_meta(context, tcp_fd);
    if (r < 0) {
        std::cout << typeid(this).name() << " : " << __func__ << " send handshake msg failed." << r << std::endl;
        if (need_set_fault) {
            fault();
        }
        return r;
    }
    worker->center.create_file_event(tcp_fd, EVENT_READABLE, read_handler);
    std::cout << typeid(this).name() << " : " << __func__ << " finish " << std::endl;
    return 0;
}

void RDMAConnectedSocketImpl::handle_connection() {
    std::cout << typeid(this).name() << " : " << __func__ << " QP: " << local_qpn << " tcp_fd: " << tcp_fd << " notify_fd: " << notify_fd
              << std::endl;
    int r = qp->recv_cm_meta(context, tcp_fd);
    if (r <= 0) {
        if (r != -EAGAIN) {
            dispatcher->perf_logger->inc(l_msgr_rdma_handshake_errors);
            std::cout << typeid(this).name() << " : " << __func__ << " recv handshake msg failed." << std::endl;
            fault();
        }
        return;
    }

    if (1 == connected) {
        std::cout << typeid(this).name() << " : " << __func__ << " warnning: logic failed: read len: " << r << std::endl;
        fault();
        return;
    }

    if (!is_server) {  // first time: cm meta sync + ack from server
        if (!connected) {
            r = activate();
            kassert(!r);
        }
        notify();
        r = qp->send_cm_meta(context, tcp_fd);
        if (r < 0) {
            std::cout << typeid(this).name() << " : " << __func__ << " send client ack failed." << std::endl;
            dispatcher->perf_logger->inc(l_msgr_rdma_handshake_errors);
            fault();
        }
    } else {
        if (qp->get_peer_cm_meta().peer_qpn == 0) {  // first time: cm meta sync from client
            if (active) {
                std::cout << typeid(this).name() << " : " << __func__ << " server is already active." << std::endl;
                return;
            }
            r = activate();
            kassert(!r);
            r = qp->send_cm_meta(context, tcp_fd);
            if (r < 0) {
                std::cout << typeid(this).name() << " : " << __func__ << " server ack failed." << std::endl;
                dispatcher->perf_logger->inc(l_msgr_rdma_handshake_errors);
                fault();
                return;
            }
        } else {  // second time: cm meta ack from client
            connected = 1;
            std::cout << typeid(this).name() << " : " << __func__ << " handshake of rdma is done. server connected: " << connected << std::endl;
            // cleanup();
            submit(false);
            notify();
        }
    }
}

ssize_t RDMAConnectedSocketImpl::read(char *buf, size_t len) {
    eventfd_t event_val = 0;
    int r = eventfd_read(notify_fd, &event_val);
    // std::cout << typeid(this).name() << " : " << __func__ << " notify_fd : " << event_val << " in " << local_qpn << " r = " << r << std::endl;

    if (r == -1 && errno != EAGAIN) {
        std::cout << typeid(this).name() << " : " << __func__ << " notify_fd : " << event_val << " in " << local_qpn << " r = " << r << std::endl;
        std::cout << "READ fd FAILED: " << cpp_strerror(errno) << std::endl;
    }

    if (!active) {
        std::cout << typeid(this).name() << " : " << __func__ << " when ib not active. len: " << len << std::endl;
        return -EAGAIN;
    }

    if (0 == connected && !is_server) {
        // std::cout << typeid(this).name() << " : " << __func__ << " when ib not connected. len: " << len << std::endl;
        return -EAGAIN;
    }
    ssize_t read = 0;
    read = read_buffers(buf, len);

    if (is_server && connected == 0) {
        std::cout << typeid(this).name() << " : " << __func__ << " we do not need last handshake, QP: " << local_qpn << " peer QP: " << peer_qpn
                  << std::endl;
        connected = 1;  // if so, we don't need the last handshake
        cleanup();
        submit(false);
    }

    if (!buffers.empty()) {
        notify();
    }

    if (read == 0 && error) return -error;
    return read == 0 ? -EAGAIN : read;
}

void RDMAConnectedSocketImpl::buffer_prefetch(void) {
    std::vector<ibv_wc> cqe;
    get_wc(cqe);
    if (cqe.empty()) return;

    for (size_t i = 0; i < cqe.size(); ++i) {
        ibv_wc *response = &cqe[i];
        kassert(response->status == IBV_WC_SUCCESS);
        Chunk *chunk = reinterpret_cast<Chunk *>(response->wr_id);
        chunk->prepare_read(response->byte_len);

        if (chunk->get_size() == 0) {
            chunk->reset_read_chunk();
            dispatcher->perf_logger->inc(l_msgr_rdma_rx_fin);
            if (connected) {
                error = ECONNRESET;
                std::cout << typeid(this).name() << " : " << __func__ << " got remote close msg..." << std::endl;
            }
            dispatcher->recall_chunk_to_pool(chunk);
            continue;
        } else {
            buffers.push_back(chunk);
            std::cout << typeid(this).name() << " : " << __func__ << " buffers add a chunk: " << chunk->get_offset() << ":" << chunk->get_bound()
                      << std::endl;
        }
    }
    worker->perf_logger->inc(l_msgr_rdma_rx_chunks, cqe.size());
}

ssize_t RDMAConnectedSocketImpl::read_buffers(char *buf, size_t len) {
    size_t read_size = 0, tmp = 0;
    buffer_prefetch();
    auto pchunk = buffers.begin();
    while (pchunk != buffers.end()) {
        tmp = (*pchunk)->read(buf + read_size, len - read_size);
        read_size += tmp;
        std::cout << typeid(this).name() << " : " << __func__ << " read chunk " << *pchunk << " bytes length" << tmp
                  << " offset: " << (*pchunk)->get_offset() << " ,bound: " << (*pchunk)->get_bound() << std::endl;

        if ((*pchunk)->get_size() == 0) {
            (*pchunk)->reset_read_chunk();
            dispatcher->recall_chunk_to_pool(*pchunk);
            update_post_backlog();
            std::cout << typeid(this).name() << " : " << __func__ << " read over one chunk " << std::endl;
            pchunk++;
        }

        if (read_size == len) {
            break;
        }
    }

    buffers.erase(buffers.begin(), pchunk);
    // std::cout << typeid(this).name() << " : " << __func__ << " got " << read_size << " bytes, buffers size: " << buffers.size() << std::endl;
    worker->perf_logger->inc(l_msgr_rdma_rx_bytes, read_size);
    return read_size;
}

ssize_t RDMAConnectedSocketImpl::send(BufferList &bl, bool more) {
    if (error) {
        if (!active) return -EPIPE;
        return -error;
    }
    size_t bytes = bl.get_len();
    if (!bytes) return 0;
    {
        std::lock_guard l{lock};
        pending_bl.Append(bl);
        if (!connected) {
            std::cout << typeid(this).name() << " : " << __func__ << " fake send to upper, QP: " << local_qpn << std::endl;
            return bytes;
        }
    }
    std::cout << typeid(this).name() << " : " << __func__ << " QP: " << local_qpn << std::endl;
    ssize_t r = submit(more);
    if (r < 0 && r != -EAGAIN) return r;
    return bytes;
}

size_t RDMAConnectedSocketImpl::tx_copy_chunk(std::vector<Chunk *> &tx_buffers, size_t req_copy_len, decltype(pending_bl.get_begin()) &start,
                                              const decltype(pending_bl.get_end()) &end) {
    kassert(start != end);
    auto chunk_idx = tx_buffers.size();
    if (0 == worker->get_reged_mem(this, tx_buffers, req_copy_len)) {
        std::cout << typeid(this).name() << " : " << __func__ << " no enough buffers in worker " << worker << std::endl;
        worker->perf_logger->inc(l_msgr_rdma_tx_no_mem);
        return 0;
    }

    Chunk *current_chunk = tx_buffers[chunk_idx];
    size_t write_len = 0;
    while (start != end) {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(start->get_buffer());

        size_t slice_write_len = 0;
        while (slice_write_len < start->get_len()) {
            size_t real_len = current_chunk->write((char *)addr + slice_write_len, start->get_len() - slice_write_len);

            slice_write_len += real_len;
            write_len += real_len;
            req_copy_len -= real_len;

            if (current_chunk->full()) {
                if (++chunk_idx == tx_buffers.size()) return write_len;
                current_chunk = tx_buffers[chunk_idx];
            }
        }

        ++start;
    }
    kassert(req_copy_len == 0);
    return write_len;
}

ssize_t RDMAConnectedSocketImpl::submit(bool more) {
    if (error) return -error;
    std::lock_guard l{lock};
    size_t bytes = pending_bl.get_len();
    std::cout << typeid(this).name() << " : " << __func__ << " we need " << bytes << " bytes. iov size: " << pending_bl.GetSize() << std::endl;
    if (!bytes) return 0;

    std::vector<Chunk *> tx_buffers;
    auto it = pending_bl.get_begin();
    auto copy_start = it;
    size_t total_copied = 0, wait_copy_len = 0;
    while (it != pending_bl.get_end()) {
        kassert(ib->is_tx_buffer(static_cast<const char *>(it->get_buffer())));
        // if (ib->is_tx_buffer(static_cast<const char *>(it->get_buffer()))) {
        kassert(wait_copy_len == 0);
        /*
        if (unlikely(wait_copy_len)) {
            std::cout << __func__ << " the buffer is not a registerred buffer, geting" << std::endl;
            size_t copied = tx_copy_chunk(tx_buffers, wait_copy_len, copy_start, it);
            total_copied += copied;
            if (copied < wait_copy_len) goto sending;
            wait_copy_len = 0;
        }*/
        kassert(copy_start == it);
        tx_buffers.push_back(ib->get_tx_chunk_by_buffer(static_cast<const char *>(it->get_buffer())));
        total_copied += it->get_len();
        ++copy_start;
        /*}
         else {
             wait_copy_len += it->get_len();
         }
         */
        ++it;
    }
    kassert(wait_copy_len == 0);
    /*
    if (unlikely(wait_copy_len)) {
        total_copied += tx_copy_chunk(tx_buffers, wait_copy_len, copy_start, it);
    }
    */
sending:

    if (total_copied == 0) return -EAGAIN;
    // kassert(total_copied == pending_bl.get_len());
    pending_bl.Clear();

    std::cout << typeid(this).name() << " : " << __func__ << " left bytes: " << pending_bl.get_len() << " in buffers " << pending_bl.GetSize()
              << " tx chunks " << tx_buffers.size() << std::endl;

    int r = post_work_request(tx_buffers);
    if (r < 0) return r;

    std::cout << typeid(this).name() << " : " << __func__ << " finished sending " << total_copied << " bytes." << std::endl;
    return pending_bl.get_len() ? -EAGAIN : 0;
}

int RDMAConnectedSocketImpl::post_work_request(std::vector<Chunk *> &tx_buffers) {
    std::cout << typeid(this).name() << " : " << __func__ << " QP: " << local_qpn << " numchunks: " << tx_buffers.size() << std::endl;
    auto current_buffer = tx_buffers.begin();
    ibv_sge isge[tx_buffers.size()];
    uint32_t current_sge = 0;
    ibv_send_wr iswr[tx_buffers.size()];
    uint32_t current_swr = 0;
    ibv_send_wr *pre_wr = NULL;
    uint32_t num = 0;

    // FIPS zeroization audit 20191115: these memsets are not security related.
    memset(iswr, 0, sizeof(iswr));
    memset(isge, 0, sizeof(isge));

    while (current_buffer != tx_buffers.end()) {
        isge[current_sge].addr = reinterpret_cast<uint64_t>((*current_buffer)->buffer);
        isge[current_sge].length = (*current_buffer)->get_offset();
        isge[current_sge].lkey = (*current_buffer)->mr->lkey;
        std::cout << typeid(this).name() << " : " << __func__ << " sending buffer: " << *current_buffer << " length: " << isge[current_sge].length
                  << std::endl;

        iswr[current_swr].wr_id = reinterpret_cast<uint64_t>(*current_buffer);
        iswr[current_swr].next = NULL;
        iswr[current_swr].sg_list = &isge[current_sge];
        iswr[current_swr].num_sge = 1;
        iswr[current_swr].opcode = IBV_WR_SEND;
        iswr[current_swr].send_flags = IBV_SEND_SIGNALED;

        num++;
        worker->perf_logger->inc(l_msgr_rdma_tx_bytes, isge[current_sge].length);
        if (pre_wr) pre_wr->next = &iswr[current_swr];
        pre_wr = &iswr[current_swr];
        ++current_sge;
        ++current_swr;
        ++current_buffer;
    }

    ibv_send_wr *bad_tx_work_request = nullptr;
    if (ibv_post_send(qp->get_qp(), iswr, &bad_tx_work_request)) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to send data"
                  << " (most probably should be peer not ready): " << cpp_strerror(errno) << std::endl;
        worker->perf_logger->inc(l_msgr_rdma_tx_failed);
        return -errno;
    }
    worker->perf_logger->inc(l_msgr_rdma_tx_chunks, tx_buffers.size());
    std::cout << typeid(this).name() << " : " << __func__ << " qp state is " << get_qp_state() << std::endl;
    return 0;
}

void RDMAConnectedSocketImpl::fin() {
    ibv_send_wr wr;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&wr, 0, sizeof(wr));

    wr.wr_id = reinterpret_cast<uint64_t>(qp);
    wr.num_sge = 0;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    ibv_send_wr *bad_tx_work_request = nullptr;
    if (ibv_post_send(qp->get_qp(), &wr, &bad_tx_work_request)) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to send message="
                  << " ibv_post_send failed(most probably should be peer not ready): " << cpp_strerror(errno) << std::endl;
        worker->perf_logger->inc(l_msgr_rdma_tx_failed);
        return;
    }
}

void RDMAConnectedSocketImpl::cleanup() {
    if (read_handler && tcp_fd >= 0) {
        (static_cast<C_handle_connection_read *>(read_handler))->close();
        worker->center.submit_to(
            worker->center.get_id(), [this]() { worker->center.delete_file_event(tcp_fd, EVENT_READABLE | EVENT_WRITABLE); }, false);
        delete read_handler;
        read_handler = nullptr;
    }
    if (established_handler) {
        (static_cast<C_handle_connection_established *>(established_handler))->close();
        delete established_handler;
        established_handler = nullptr;
    }
}

void RDMAConnectedSocketImpl::notify() {
    eventfd_t event_val = 1;
    int r = eventfd_write(notify_fd, event_val);
    kassert(r == 0);
}

void RDMAConnectedSocketImpl::shutdown() {
    if (!error) fin();
    error = ECONNRESET;
    active = false;
}

void RDMAConnectedSocketImpl::close() {
    if (!error) fin();
    error = ECONNRESET;
    active = false;
}

void RDMAConnectedSocketImpl::fault() {
    std::cout << typeid(this).name() << " : " << __func__ << " tcp fd " << tcp_fd << std::endl;
    error = ECONNRESET;
    connected = 1;
    notify();
}

void RDMAConnectedSocketImpl::set_accept_fd(int sd) {
    tcp_fd = sd;
    is_server = true;
    worker->center.submit_to(
        worker->center.get_id(), [this]() { worker->center.create_file_event(tcp_fd, EVENT_READABLE, read_handler); }, true);
}

void RDMAConnectedSocketImpl::post_chunks_to_rq(int num) { post_backlog += num - ib->post_chunks_to_rq(num, qp); }

void RDMAConnectedSocketImpl::update_post_backlog() {
    if (post_backlog) post_backlog -= post_backlog - dispatcher->post_chunks_to_rq(post_backlog, qp);
}
