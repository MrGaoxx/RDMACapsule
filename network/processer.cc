#include "network/processer.h"

#include <unistd.h>

#include "core/server.h"

class Processor::C_processor_accept : public EventCallback {
    Processor* pro;

   public:
    explicit C_processor_accept(Processor* p) : pro(p) {}
    void do_request(uint64_t id) override { pro->accept(); }
};

Processor::Processor(Server* s, Worker* w, Context* c, NetworkStack* ns)
    : server(s), worker(w), context(c), stack(ns), listen_handler(new C_processor_accept(this)) {}

int Processor::bind(const entity_addr_t& bind_addr, entity_addr_t* bound_addr) {
    // bind to socket(s)
    std::cout << typeid(this).name() << " : " << __func__ << " " << bind_addr << std::endl;

    SocketOptions opts;
    opts.nodelay = context->m_rdma_config_->m_tcp_nodelay_;
    opts.rcbuf_size = context->m_rdma_config_->m_tcp_rcvbuf_;

    *bound_addr = bind_addr;
    auto& listen_addr = *bound_addr;
    /* bind to port */
    int r = -1;

    for (int i = 0; i < context->m_rdma_config_->m_bind_retry_count_; i++) {
        if (i > 0) {
            std::cout << typeid(this).name() << " : " << __func__ << " was unable to bind. Trying again in "
                      << context->m_rdma_config_->m_bind_retry_delay_seconds_ << " seconds " << std::endl;
            sleep(context->m_rdma_config_->m_bind_retry_delay_seconds_);
        }

        if (!listen_addr.get_port()) {
            listen_addr.set_port(context->m_rdma_config_->m_listen_port);
        }
        worker->center.submit_to(
            worker->center.get_id(), [this, &listen_addr, &opts, &r]() { r = worker->listen(listen_addr, opts, &listen_socket); }, false);
        if (r < 0) {
            std::cout << typeid(this).name() << " : " << __func__ << " unable to bind to " << listen_addr << ": " << cpp_strerror(r) << std::endl;
            continue;
        }
        if (r == 0) {
            break;
        }
    }
    // It seems that binding completely failed, return with that exit status
    if (r < 0) {
        std::cout << typeid(this).name() << " : " << __func__ << " was unable to bind after " << context->m_rdma_config_->m_bind_retry_count_
                  << " attempts: " << cpp_strerror(r) << std::endl;
        // clean up previous bind
        listen_socket.abort_accept();

        return r;
    }

    std::cout << typeid(this).name() << " : " << __func__ << " bound to " << *bound_addr << std::endl;
    return 0;
}

void Processor::start() {
    std::cout << typeid(this).name() << " : " << __func__ << std::endl;

    if (is_started) {
        return;
    }
    // start thread
    worker->center.submit_to(
        worker->center.get_id(),
        [this]() {
            if (listen_socket) {
                if (listen_socket.fd() == -1) {
                    std::cout << typeid(this).name() << " : " << __func__ << " Error: processor restart after listen_socket.fd closed. " << this
                              << std::endl;
                    return;
                }
                worker->center.create_file_event(listen_socket.fd(), EVENT_READABLE, listen_handler);
            }
        },
        false);
    is_started = true;
}

void Processor::accept() {
    SocketOptions opts;
    opts.nodelay = context->m_rdma_config_->m_tcp_nodelay_;
    opts.rcbuf_size = context->m_rdma_config_->m_tcp_rcvbuf_;
    opts.priority = context->m_rdma_config_->m_tcp_priority_;

    std::cout << typeid(this).name() << " : " << __func__ << " listen_fd=" << listen_socket.fd() << std::endl;
    unsigned accept_error_num = 0;

    while (true) {
        entity_addr_t addr;
        ConnectedSocket cli_socket;
        Worker* w = worker;

        w = stack->get_worker();
        int r = listen_socket.accept(&cli_socket, opts, &addr, w);
        if (r == 0) {
            std::cout << typeid(this).name() << " : " << __func__ << " accepted incoming on sd " << cli_socket.fd() << std::endl;
            server->accept(w, std::move(cli_socket), listen_socket.get_addr(), addr);
            accept_error_num = 0;
            continue;
        } else {
            --w->references;
            if (r == -EINTR) {
                continue;
            } else if (r == -EAGAIN) {
                break;
            } else if (r == -EMFILE || r == -ENFILE) {
                std::cout << typeid(this).name() << " : " << __func__ << " open file descriptions limit reached sd = " << listen_socket.fd()
                          << " errno " << r << " " << cpp_strerror(r) << std::endl;
                if (++accept_error_num > context->m_rdma_config_->m_max_accept_failures_) {
                    std::cout << "Proccessor accept has encountered enough error numbers, just do abort()." << std::endl;
                    abort();
                }
                continue;
            } else if (r == -ECONNABORTED) {
                std::cout << typeid(this).name() << " : " << __func__ << " it was closed because of rst arrived sd = " << listen_socket.fd()
                          << " errno " << r << " " << cpp_strerror(r) << std::endl;
                continue;
            } else {
                std::cout << typeid(this).name() << " : " << __func__ << " no incoming connection?"
                          << " errno " << r << " " << cpp_strerror(r) << std::endl;
                if (++accept_error_num > context->m_rdma_config_->m_max_accept_failures_) {
                    std::cout << "Proccessor accept has encountered enough error numbers, just do abort()." << std::endl;
                    abort();
                }
                continue;
            }
        }
    }
}

void Processor::stop() {
    std::cout << typeid(this).name() << " : " << __func__ << std::endl;

    worker->center.submit_to(
        worker->center.get_id(),
        [this]() {
            if (listen_socket) {
                worker->center.delete_file_event(listen_socket.fd(), EVENT_READABLE);
                listen_socket.abort_accept();
            }
        },
        false);
    is_started = false;
}
