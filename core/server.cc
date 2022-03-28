#include "core/server.h"

Server::Server(Context *c) : context(c), accepting_conns(), conns() {
    stack = NetworkStack::create(context, "rdma");
    unsigned processor_num = 1;
    processor_num = stack->get_num_worker();
    for (unsigned i = 0; i < processor_num; ++i) processors.push_back(new Processor(this, stack->get_worker(i), c, stack.get()));
}

int Server::bind(const entity_addr_t &bind_addr) {
    lock.lock();

    if (!stack->is_ready()) {
        std::cout << __func__ << " Network Stack is not ready for bind yet - postponed" << std::endl;
        return 0;
    }

    lock.unlock();

    entity_addr_t bound_addr;
    unsigned i = 0;
    // choose a random prorcessor to bind
    auto &&p = processors[conn_count % processors.size()];
    int r = p->bind(bind_addr, &bound_addr);
    if (r) {
        return r;
    }
    ++conn_count;
    return 0;
}

Connection *Server::create_connect(const entity_addr_t &addr) {
    std::lock_guard l{lock};

    std::cout << __func__ << " " << addr << ", creating connection and registering" << std::endl;

    // here is where we decide which of the addrs to connect to.  always prefer
    // the first one, if we support it.
    entity_addr_t target = addr;
    // create connection
    Worker *w = stack->get_worker();
    auto conn = new Connection(context, this, w);
    conn->connect(target);

    std::cout << __func__ << " " << conn << " " << addr << std::endl;
    conns[target] = conn;
    return conn;
}

void Server::accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr) {
    std::lock_guard l{lock};
    auto conn = new Connection(context, this, w);
    conn->accept(std::move(cli_socket), listen_addr, peer_addr);
    accepting_conns.insert(conn);
}

Server::~Server() {}