#ifndef CORE_SERVER_H
#define CORE_SERVER_H
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

    virtual int Send(entity_addr_t dst, BufferList bl) { conns[dst]->Send(bl); }
    virtual ssize_t Read(entity_addr_t addr, char *buf, size_t n) {
        // tbd
        kassert(accepting_conns.size() == 1);
        kassert(addr == (*accepting_conns.begin())->get_local_addr());
        return (*accepting_conns.begin())->Read(buf, n);
    };

    virtual NetworkStack *get_network_stack() { return stack.get(); }
    virtual RDMAStack *get_rdma_stack() { return reinterpret_cast<RDMAStack *>(stack.get()); }
    std::function<void(Connection *)> *conn_read_callback;
    std::function<void(Connection *)> *conn_write_callback;

   protected:
    std::shared_ptr<NetworkStack> stack;
    std::vector<Processor *> processors;
    std::unordered_map<entity_addr_t, Connection *> conns;
    std::set<Connection *> accepting_conns;
    Context *context;

    std::mutex lock;
    int conn_count;
};

class MulticastDaemon : public Server {
   public:
    MulticastDaemon(Context *context);
    ~MulticastDaemon();

    // int bind(const entity_addr_t &bind_addr);
    void accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr) override;

    std::function<void(Connection *)> *conn_read_callback;
    std::function<void(Connection *)> *conn_write_callback;

   private:
    static const int kNumMulticasts = 2;
    std::unordered_map<entity_addr_t, std::array<entity_addr_t, kNumMulticasts>> multicast_addrs;
    void process_client_read(Connection *);
    void process_server_read(Connection *);
};

#endif