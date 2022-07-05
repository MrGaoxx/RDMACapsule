#ifndef NETWORK_PROCESSER_H
#define NETWORK_PROCESSER_H
#include "common/context.h"
#include "network/Stack.h"
#include "network/net_handler.h"

class Server;
class Processor {
    Worker *worker;
    ServerSocket listen_socket;
    EventCallbackRef listen_handler;
    Context *context;
    NetworkStack *stack;
    Server *server;
    class C_processor_accept;

   public:
    Processor(Server *server, Worker *w, Context *c, NetworkStack *stack);
    ~Processor() { delete listen_handler; };

    void start();
    void stop();
    bool is_running() { return is_started; }
    int bind(const entity_addr_t &bind_addr, entity_addr_t *bound_addrs);

    void accept();

   private:
    bool is_started = false;
};
#endif
