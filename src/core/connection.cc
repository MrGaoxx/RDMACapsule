#include "connection.h"

#include "server.h"

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

class C_read_callback : public EventCallback {
    Connection *conn;

   public:
    explicit C_read_callback(Connection *c) : conn(c) {}
    void do_request(uint64_t fd) override { (*conn->read_callback)(conn); }
};

class C_write_callback : public EventCallback {
    Connection *conn;

   public:
    explicit C_write_callback(Connection *c) : conn(c) {}
    void do_request(uint64_t fd) override { (*conn->write_callback)(conn); }
};

Connection::Connection(Context *context, Server *s, Worker *w)
    : mc_id(0), state(STATE_NONE), stack(s->get_network_stack()), context(context), worker(w), center(&w->center), server(s) {
    read_handler = new C_handle_read(this);
    read_callback_handler = new C_read_callback(this);
    write_callback_handler = new C_write_callback(this);
}

void Connection::connect(const entity_addr_t &addr) {
    std::lock_guard<std::mutex> l(lock);
    peer_addr = addr;
    state = STATE_CONNECTING;
    // rescheduler connection in order to avoid lock dep
    // may called by external thread(send_message)
    center->dispatch_event_external(read_handler);
}

void Connection::process() {
    std::lock_guard<std::mutex> l(lock);
    std::cout << typeid(this).name() << __func__ << " cm_id:" << mc_id << " peer_addr " << peer_addr << " : "
              << " state: " << state << std::endl;

    switch (state) {
        case STATE_NONE: {
            return;
        }
        case STATE_CLOSED: {
            return;
        }
        case STATE_CONNECTING: {
            if (cs) {
                std::cout << typeid(this).name() << __func__ << "!!! closing existing connected socket when conecting" << std::endl;
                center->delete_file_event(cs.fd(), EVENT_READABLE | EVENT_WRITABLE);
                cs.close();
            }

            SocketOptions opts;
            opts.nonblock = true;
            opts.priority = context->m_rdma_config_->m_tcp_priority_;
            opts.connect_bind_addr = context->m_rdma_config_->m_addr;

            ssize_t r = worker->connect(peer_addr, opts, &cs);
            if (unlikely(r < 0)) {
                std::cout << __func__ << ": worker connect failed" << std::endl;
                return;
            }
            kassert(cs.is_connected() <= 0);
            center->create_file_event(cs.fd(), EVENT_READABLE, read_callback_handler);
            center->create_file_event(cs.fd(), EVENT_WRITABLE, read_handler);
            state = STATE_CONNECTING_RE;
            return;
        }
        case STATE_CONNECTING_RE: {
            ssize_t r = cs.is_connected();
            if (r < 0) {
                std::cout << typeid(this).name() << " : " << __func__ << " reconnect failed to " << peer_addr << std::endl;
                if (r == -ECONNREFUSED) {
                    std::cout << typeid(this).name() << " : " << __func__ << " connection refused!" << std::endl;
                }
                return;
            } else if (r == 0) {
                std::cout << typeid(this).name() << " : " << __func__ << " nonblock connect inprogress" << std::endl;
                if (stack->nonblock_connect_need_writable_event()) {
                    center->create_file_event(cs.fd(), EVENT_WRITABLE, read_handler);
                }
                return;
            }
            state = STATE_CONNECTION_ESTABLISHED;
            center->delete_file_event(cs.fd(), EVENT_WRITABLE);
            std::cout << typeid(this).name() << " : " << __func__ << " connect successfully, ready to send banner" << std::endl;
            center->dispatch_event_external(write_callback_handler);
            break;
        }

        case STATE_ACCEPTING: {
            center->create_file_event(cs.fd(), EVENT_READABLE, read_callback_handler);
            center->dispatch_event_external(write_callback_handler);
            state = STATE_CONNECTION_ESTABLISHED;
            break;
        }

        case STATE_CONNECTION_ESTABLISHED: {
            break;
        }
    }
}

void Connection::accept(ConnectedSocket socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr_) {
    std::cout << typeid(this).name() << " : " << __func__ << " sd=" << socket.fd() << " listen_addr " << listen_addr << " peer_addr " << peer_addr_
              << std::endl;
    kassert(socket.fd() >= 0);

    std::lock_guard<std::mutex> l(lock);
    cs = std::move(socket);
    local_addr = listen_addr;
    peer_addr = peer_addr_;  // until we know better
    state = STATE_ACCEPTING;
    // rescheduler connection in order to avoid lock dep
    center->dispatch_event_external(read_handler);
}
