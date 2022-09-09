#ifndef CORE_SERVER_H
#define CORE_SERVER_H
#include "core/Infiniband.h"
#include "core/connection.h"
#include "network/processer.h"
class RDMAStack;

class Server {
   public:
    Server(Context *context);
    ~Server();

    virtual void start();
    virtual void stop();

    int bind(const entity_addr_t &bind_addr);
    virtual void accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr);

    virtual Connection *create_connect(const entity_addr_t &addr);

    virtual int send(entity_addr_t dst, std::vector<Infiniband::MemoryManager::Chunk *> &chunks) { return conns[dst]->Send(chunks); }
    virtual int send(entity_addr_t dst, BufferList bl) { return conns[dst]->Send(bl); }
    void set_txc_callback(entity_addr_t dst, std::function<void(Infiniband::MemoryManager::Chunk *)> cb) { conns[dst]->set_txc_callback(cb); }
    virtual void drain() {
        // tbd
        kassert(accepting_conns.size() == 1);
        (*accepting_conns.begin())->drain();
        return;
    };
    virtual ssize_t read(entity_addr_t addr, char *buf, size_t n) {
        // tbd
        kassert(accepting_conns.size() == 1);
        kassert(addr == (*accepting_conns.begin())->get_local_addr());
        return (*accepting_conns.begin())->Read(buf, n);
    };

    virtual NetworkStack *get_network_stack() { return stack.get(); }
    virtual RDMAStack *get_rdma_stack() { return reinterpret_cast<RDMAStack *>(stack.get()); }
    std::function<void(Connection *)> *conn_read_callback_p;
    std::function<void(Connection *)> *conn_write_callback_p;

   protected:
    std::shared_ptr<NetworkStack> stack;
    std::vector<Processor *> processors;
    std::unordered_map<entity_addr_t, Connection *> conns;
    std::set<Connection *> accepting_conns;

    Context *context;

    std::mutex lock;
    int conn_count;
};

#endif