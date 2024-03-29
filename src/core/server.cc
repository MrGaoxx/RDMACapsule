#include "core/server.h"

#include "core/Infiniband.h"

Server::Server(Context *c) : accepting_conns(), context(c) {
    if (context->m_rdma_config_->m_use_rdma_) {
        stack = NetworkStack::create(context, "rdma");
    } else {
        std::cout << "RDMA not enabled, use posix" << std::endl;
        stack = NetworkStack::create(context, "posix");
    }
    unsigned processor_num = 6;
    processor_num = stack->get_num_worker();
    for (unsigned i = 0; i < processor_num; ++i) processors.push_back(new Processor(this, stack->get_worker(i), c, stack.get()));
}

void Server::start() {
    stack->start();
    stack->ready();
}

void Server::stop() {
    // tbd
}

int Server::bind(const entity_addr_t &bind_addr) {
    lock.lock();

    if (!stack->is_ready()) {
        std::cout << typeid(this).name() << " : " << __func__ << " Network Stack is not ready for bind yet - postponed" << std::endl;
        return 0;
    }

    lock.unlock();

    entity_addr_t bound_addr;

    // choose a random prorcessor to bind
    auto &&p = processors[conn_count % processors.size()];
    if (p->is_running()) {
        std::cout << typeid(this).name() << " : " << __func__ << "listen failed, thread is running" << std::endl;
        return -EBUSY;
    }
    int r = p->bind(bind_addr, &bound_addr);
    if (r) {
        return r;
    }
    p->start();
    ++conn_count;
    return 0;
}

Connection *Server::create_connect(const entity_addr_t &addr) {
    std::lock_guard l{lock};

    std::cout << typeid(this).name() << " : " << __func__ << " " << addr << ", creating connection and registering" << std::endl;

    // here is where we decide which of the addrs to connect to.  always prefer
    // the first one, if we support it.
    entity_addr_t target = addr;
    // create connection
    Worker *w = stack->get_worker();
    auto conn = new Connection(context, this, w);
    conn->write_callback = client_conn_write_callback_p;
    conn->read_callback = client_conn_read_callback_p;
    conn->connect(target);
    std::cout << typeid(this).name() << " : " << __func__ << " " << conn << " " << addr << std::endl;
    conns[target] = conn;
    return conn;
}

void Server::accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr) {
    std::cout << typeid(this).name() << " " << __func__ << std::endl;
    std::lock_guard l{lock};
    auto conn = new Connection(context, this, w);
    conn->read_callback = server_conn_read_callback_p;
    conn->write_callback = server_conn_write_callback_p;
    conn->accept(std::move(cli_socket), listen_addr, peer_addr);
    accepting_conns.insert(conn);
}

Server::~Server() {}