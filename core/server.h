#ifndef CORE_SERVER_H
#define CORE_SERVER_H
#include "core/connection.h"
#include "network/processer.h"
class Server {
   public:
    Server(Context *context);
    ~Server();
    int bind(const entity_addr_t &bind_addr);
    Connection *create_connect(const entity_addr_t &addr);
    void accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr);

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
