#ifndef CORE_SERVER_H
#define CORE_SERVER_H
#include "core/connection.h"
#include "network/processer.h"
class RDMAStack;
class Server {
   public:
    Server(Context *context);
    void start();
    ~Server();
    int bind(const entity_addr_t &bind_addr);
    Connection *create_connect(const entity_addr_t &addr);
    void accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr);
    int Send(entity_addr_t dst, BufferList bl) { conns[dst]->Send(bl); }
    ssize_t Read(entity_addr_t addr, char *buf, size_t n) {
        // tbd
        kassert(accepting_conns.size() == 1);
        kassert(addr == (*accepting_conns.begin())->get_local_addr());
        return (*accepting_conns.begin())->Read(buf, n);
    };

    RDMAStack *get_rdma_stack() { return reinterpret_cast<RDMAStack *>(stack.get()); }

   private:
    std::shared_ptr<NetworkStack> stack;
    Context *context;
    std::vector<Processor *> processors;
    std::unordered_map<entity_addr_t, Connection *> conns;
    std::set<Connection *> accepting_conns;
    std::mutex lock;
    int conn_count;
};

#endif
