#include "connection.h"

class C_handle_read : public EventCallback {
    Connection *conn;

   public:
    explicit C_handle_read(Connection *c) : conn(c) {}
    void do_request(uint64_t fd_or_id) override { conn->process(); }
};

class C_handle_write : public EventCallback {
    Connection *conn;

   public:
    explicit C_handle_write(Connection *c) : conn(c) {}
    void do_request(uint64_t fd) override { conn->handle_write(); }
};

class C_handle_write_callback : public EventCallback {
    Connection *conn;

   public:
    explicit C_handle_write_callback(Connection *c) : conn(c) {}
    void do_request(uint64_t fd) override { conn->handle_write_callback(); }
};

class C_clean_handler : public EventCallback {
    Connection *conn;

   public:
    explicit C_clean_handler(Connection *c) : conn(c) {}
    void do_request(uint64_t id) override {
        conn->cleanup();
        delete this;
    }
};

Connection::Connection(Context *context, Server *s, Worker *w)
    : server(s),
      conn_id(q->get_id()),
      logger(w->get_perf_counter()),
      state(STATE_NONE),
      port(-1),
      recv_buf(NULL),
      recv_max_prefetch(std::max<int64_t>(msgr->cct->_conf->ms_tcp_prefetch_max_size, TCP_PREFETCH_MIN_SIZE)),
      recv_start(0),
      recv_end(0),
      connect_timeout_us(cct->_conf->ms_connection_ready_timeout * 1000 * 1000),
      inactive_timeout_us(cct->_conf->ms_connection_idle_timeout * 1000 * 1000),
      msgr2(m2),
      state_offset(0),
      worker(w),
      center(&w->center),
      read_buffer(nullptr) {
    read_handler = new C_handle_read(this);
    write_handler = new C_handle_write(this);
    write_callback_handler = new C_handle_write_callback(this);
    // double recv_max_prefetch see "read_until"
    recv_buf = new char[2 * recv_max_prefetch];
}

Connection::~Connection() {
    if (recv_buf) delete[] recv_buf;
    ceph_assert(!delay_state);
}

ssize_t Connection::read(unsigned len, char *buffer, std::function<void(char *, ssize_t)> callback) {
    ldout(async_msgr->cct, 20) << __func__ << (pendingReadLen ? " continue" : " start") << " len=" << len << dendl;
    ssize_t r = read_until(len, buffer);
    if (r > 0) {
        readCallback = callback;
        pendingReadLen = len;
        read_buffer = buffer;
    }
    return r;
}

// Because this func will be called multi times to populate
// the needed buffer, so the passed in bufferptr must be the same.
// Normally, only "read_message" will pass existing bufferptr in
//
// And it will uses readahead method to reduce small read overhead,
// "recv_buf" is used to store read buffer
//
// return the remaining bytes, 0 means this buffer is finished
// else return < 0 means error
ssize_t Connection::read_until(unsigned len, char *p) {
    ldout(async_msgr->cct, 25) << __func__ << " len is " << len << " state_offset is " << state_offset << dendl;

    if (async_msgr->cct->_conf->ms_inject_socket_failures && cs) {
        if (rand() % async_msgr->cct->_conf->ms_inject_socket_failures == 0) {
            ldout(async_msgr->cct, 0) << __func__ << " injecting socket failure" << dendl;
            cs.shutdown();
        }
    }

    ssize_t r = 0;
    uint64_t left = len - state_offset;
    if (recv_end > recv_start) {
        uint64_t to_read = std::min<uint64_t>(recv_end - recv_start, left);
        memcpy(p, recv_buf + recv_start, to_read);
        recv_start += to_read;
        left -= to_read;
        ldout(async_msgr->cct, 25) << __func__ << " got " << to_read << " in buffer "
                                   << " left is " << left << " buffer still has " << recv_end - recv_start << dendl;
        if (left == 0) {
            state_offset = 0;
            return 0;
        }
        state_offset += to_read;
    }

    recv_end = recv_start = 0;
    /* nothing left in the prefetch buffer */
    if (left > (uint64_t)recv_max_prefetch) {
        /* this was a large read, we don't prefetch for these */
        do {
            r = read_bulk(p + state_offset, left);
            ldout(async_msgr->cct, 25) << __func__ << " read_bulk left is " << left << " got " << r << dendl;
            if (r < 0) {
                ldout(async_msgr->cct, 1) << __func__ << " read failed" << dendl;
                return -1;
            } else if (r == static_cast<int>(left)) {
                state_offset = 0;
                return 0;
            }
            state_offset += r;
            left -= r;
        } while (r > 0);
    } else {
        do {
            r = read_bulk(recv_buf + recv_end, recv_max_prefetch);
            ldout(async_msgr->cct, 25) << __func__ << " read_bulk recv_end is " << recv_end << " left is " << left << " got " << r << dendl;
            if (r < 0) {
                ldout(async_msgr->cct, 1) << __func__ << " read failed" << dendl;
                return -1;
            }
            recv_end += r;
            if (r >= static_cast<int>(left)) {
                recv_start = len - state_offset;
                memcpy(p + state_offset, recv_buf, recv_start);
                state_offset = 0;
                return 0;
            }
            left -= r;
        } while (r > 0);
        memcpy(p + state_offset, recv_buf, recv_end - recv_start);
        state_offset += (recv_end - recv_start);
        recv_end = recv_start = 0;
    }
    ldout(async_msgr->cct, 25) << __func__ << " need len " << len << " remaining " << len - state_offset << " bytes" << dendl;
    return len - state_offset;
}

/* return -1 means `fd` occurs error or closed, it should be closed
 * return 0 means EAGAIN or EINTR */
ssize_t Connection::read_bulk(char *buf, unsigned len) {
    ssize_t nread;
again:
    nread = cs.read(buf, len);
    if (nread < 0) {
        if (nread == -EAGAIN) {
            nread = 0;
        } else if (nread == -EINTR) {
            goto again;
        } else {
            ldout(async_msgr->cct, 1) << __func__ << " reading from fd=" << cs.fd() << " : " << nread << " " << strerror(nread) << dendl;
            return -1;
        }
    } else if (nread == 0) {
        ldout(async_msgr->cct, 1) << __func__ << " peer close file descriptor " << cs.fd() << dendl;
        return -1;
    }
    return nread;
}

ssize_t Connection::write(ceph::buffer::list &bl, std::function<void(ssize_t)> callback, bool more) {
    std::unique_lock<std::mutex> l(write_lock);
    outgoing_bl.claim_append(bl);
    ssize_t r = _try_send(more);
    if (r > 0) {
        writeCallback = callback;
    }
    return r;
}

// return the remaining bytes, it may larger than the length of ptr
// else return < 0 means error
ssize_t Connection::_try_send(bool more) {
    if (async_msgr->cct->_conf->ms_inject_socket_failures && cs) {
        if (rand() % async_msgr->cct->_conf->ms_inject_socket_failures == 0) {
            ldout(async_msgr->cct, 0) << __func__ << " injecting socket failure" << dendl;
            cs.shutdown();
        }
    }

    ceph_assert(center->in_thread());
    ldout(async_msgr->cct, 25) << __func__ << " cs.send " << outgoing_bl.length() << " bytes" << dendl;
    // network block would make ::send return EAGAIN, that would make here looks
    // like do not call cs.send() and r = 0
    ssize_t r = 0;
    if (likely(!inject_network_congestion())) {
        r = cs.send(outgoing_bl, more);
    }
    if (r < 0) {
        ldout(async_msgr->cct, 1) << __func__ << " send error: " << cpp_strerror(r) << dendl;
        return r;
    }

    ldout(async_msgr->cct, 10) << __func__ << " sent bytes " << r << " remaining bytes " << outgoing_bl.length() << dendl;

    if (!open_write && is_queued()) {
        center->create_file_event(cs.fd(), EVENT_WRITABLE, write_handler);
        open_write = true;
    }

    if (open_write && !is_queued()) {
        center->delete_file_event(cs.fd(), EVENT_WRITABLE);
        open_write = false;
        if (writeCallback) {
            center->dispatch_event_external(write_callback_handler);
        }
    }

    return outgoing_bl.length();
}

void Connection::process() {
    std::lock_guard<std::mutex> l(lock);
    last_active = ceph::coarse_mono_clock::now();
    recv_start_time = ceph::mono_clock::now();

    ldout(async_msgr->cct, 20) << __func__ << dendl;

    switch (state) {
        case STATE_NONE: {
            ldout(async_msgr->cct, 20) << __func__ << " enter none state" << dendl;
            return;
        }
        case STATE_CLOSED: {
            ldout(async_msgr->cct, 20) << __func__ << " socket closed" << dendl;
            return;
        }
        case STATE_CONNECTING: {
            ceph_assert(!policy.server);

            if (cs) {
                center->delete_file_event(cs.fd(), EVENT_READABLE | EVENT_WRITABLE);
                cs.close();
            }

            SocketOptions opts;
            opts.priority = async_msgr->get_socket_priority();
            opts.connect_bind_addr = msgr->get_myaddrs().front();
            ssize_t r = worker->connect(target_addr, opts, &cs);
            if (r < 0) {
                return;
            }

            center->create_file_event(cs.fd(), EVENT_READABLE, read_handler);
            state = STATE_CONNECTING_RE;
        }
        case STATE_CONNECTING_RE: {
            ssize_t r = cs.is_connected();
            if (r < 0) {
                ldout(async_msgr->cct, 1) << __func__ << " reconnect failed to " << target_addr << dendl;
                if (r == -ECONNREFUSED) {
                    ldout(async_msgr->cct, 2) << __func__ << " connection refused!" << dendl;
                    dispatch_queue->queue_refused(this);
                }
                return;
            } else if (r == 0) {
                ldout(async_msgr->cct, 10) << __func__ << " nonblock connect inprogress" << dendl;
                if (async_msgr->get_stack()->nonblock_connect_need_writable_event()) {
                    center->create_file_event(cs.fd(), EVENT_WRITABLE, read_handler);
                }
                logger->tinc(l_msgr_running_recv_time, ceph::mono_clock::now() - recv_start_time);
                return;
            }

            center->delete_file_event(cs.fd(), EVENT_WRITABLE);
            ldout(async_msgr->cct, 10) << __func__ << " connect successfully, ready to send banner" << dendl;
            state = STATE_CONNECTION_ESTABLISHED;
            break;
        }

        case STATE_ACCEPTING: {
            center->create_file_event(cs.fd(), EVENT_READABLE, read_handler);
            state = STATE_CONNECTION_ESTABLISHED;

            break;
        }

        case STATE_CONNECTION_ESTABLISHED: {
            if (pendingReadLen) {
                ssize_t r = read(*pendingReadLen, read_buffer, readCallback);
                if (r <= 0) {  // read all bytes, or an error occured
                    pendingReadLen.reset();
                    char *buf_tmp = read_buffer;
                    read_buffer = nullptr;
                    readCallback(buf_tmp, r);
                }
                logger->tinc(l_msgr_running_recv_time, ceph::mono_clock::now() - recv_start_time);
                return;
            }
            break;
        }
    }

    protocol->read_event();

    logger->tinc(l_msgr_running_recv_time, ceph::mono_clock::now() - recv_start_time);
}

void Connection::connect(const entity_addr_t &addr) {
    std::lock_guard<std::mutex> l(lock);
    set_peer_type(type);
    set_peer_addrs(addrs);
    policy = msgr->get_policy(type);
    target_addr = target;
    _connect();
}

void Connection::_connect() {
    ldout(async_msgr->cct, 10) << __func__ << dendl;

    state = STATE_CONNECTING;
    protocol->connect();
    // rescheduler connection in order to avoid lock dep
    // may called by external thread(send_message)
    center->dispatch_event_external(read_handler);
}

void Connection::accept(ConnectedSocket socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr) {
    ldout(async_msgr->cct, 10) << __func__ << " sd=" << socket.fd() << " listen_addr " << listen_addr << " peer_addr " << peer_addr << dendl;
    ceph_assert(socket.fd() >= 0);

    std::lock_guard<std::mutex> l(lock);
    cs = std::move(socket);
    socket_addr = listen_addr;
    target_addr = peer_addr;  // until we know better
    state = STATE_ACCEPTING;
    protocol->accept();
    // rescheduler connection in order to avoid lock dep
    center->dispatch_event_external(read_handler);
}

int Connection::send_message(Message *m) {
    FUNCTRACE(async_msgr->cct);
    lgeneric_subdout(async_msgr->cct, ms, 1) << "-- " << async_msgr->get_myaddrs() << " --> " << get_peer_addrs() << " -- " << *m << " -- " << m
                                             << " con " << this << dendl;

    if (is_blackhole()) {
        lgeneric_subdout(async_msgr->cct, ms, 0) << __func__ << ceph_entity_type_name(peer_type) << " blackhole " << *m << dendl;
        m->put();
        return 0;
    }

    // optimistic think it's ok to encode(actually may broken now)
    if (!m->get_priority()) m->set_priority(async_msgr->get_default_send_priority());

    m->get_header().src = async_msgr->get_myname();
    m->set_connection(this);

#if defined(WITH_EVENTTRACE)
    if (m->get_type() == CEPH_MSG_OSD_OP)
        OID_EVENT_TRACE_WITH_MSG(m, "SEND_MSG_OSD_OP_BEGIN", true);
    else if (m->get_type() == CEPH_MSG_OSD_OPREPLY)
        OID_EVENT_TRACE_WITH_MSG(m, "SEND_MSG_OSD_OPREPLY_BEGIN", true);
#endif

    if (is_loopback) {  // loopback connection
        ldout(async_msgr->cct, 20) << __func__ << " " << *m << " local" << dendl;
        std::lock_guard<std::mutex> l(write_lock);
        if (protocol->is_connected()) {
            dispatch_queue->local_delivery(m, m->get_priority());
        } else {
            ldout(async_msgr->cct, 10) << __func__ << " loopback connection closed."
                                       << " Drop message " << m << dendl;
            m->put();
        }
        return 0;
    }

    // we don't want to consider local message here, it's too lightweight which
    // may disturb users
    logger->inc(l_msgr_send_messages);

    protocol->send_message(m);
    return 0;
}

void Connection::_stop() {
    writeCallback.reset();
    async_msgr->unregister_conn(this);
    worker->release_worker();

    state = STATE_CLOSED;
    open_write = false;

    state_offset = 0;
    // Make sure in-queue events will been processed
    center->dispatch_event_external(EventCallbackRef(new C_clean_handler(this)));
}

bool Connection::is_queued() const { return outgoing_bl.length(); }

void Connection::shutdown_socket() {
    for (auto &&t : register_time_events) center->delete_time_event(t);
    register_time_events.clear();
    if (cs) {
        center->delete_file_event(cs.fd(), EVENT_READABLE | EVENT_WRITABLE);
        cs.shutdown();
        cs.close();
    }
}

void Connection::handle_write() {
    ldout(async_msgr->cct, 10) << __func__ << dendl;
    protocol->write_event();
}

void Connection::handle_write_callback() {
    std::lock_guard<std::mutex> l(lock);
    last_active = ceph::coarse_mono_clock::now();
    recv_start_time = ceph::mono_clock::now();
    write_lock.lock();
    if (writeCallback) {
        auto callback = *writeCallback;
        writeCallback.reset();
        write_lock.unlock();
        callback(0);
        return;
    }
    write_lock.unlock();
}

void Connection::stop(bool queue_reset) {
    lock.lock();
    bool need_queue_reset = (state != STATE_CLOSED) && queue_reset;
    protocol->stop();
    lock.unlock();
}

void Connection::cleanup() {
    shutdown_socket();
    delete read_handler;
    delete write_handler;
    delete write_callback_handler;
    delete wakeup_handler;
    if (delay_state) {
        delete delay_state;
        delay_state = NULL;
    }
}
