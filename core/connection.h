#ifndef CORE_CONNECTION_H
#define CORE_CONNECTION_H

#include "network/Stack.h"
#include "network/processer.h"
class Connection {
   public:
    Connection(Context *cct, Server *m, Worker *w);
    void accept(Worker *, ConnectedSocket &&, ServerSocket &, entity_addr_t);
    int bind(const entity_addr_t &bind_addr, entity_addr_t *bound_addrs);
    void start();

    bool is_connected();
    // Only call when AsyncConnection first construct
    void connect(const entity_addr_t &addr);

    // Only call when AsyncConnection first construct
    void accept(ConnectedSocket socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr);

    entity_addr_t get_peer_socket_addr() const { return peer_addr; }

   private:
    enum { STATE_NONE, STATE_CONNECTING, STATE_CONNECTING_RE, STATE_ACCEPTING, STATE_CONNECTION_ESTABLISHED, STATE_CLOSED };

    static const uint32_t TCP_PREFETCH_MIN_SIZE;
    static const char *get_state_name(int state) {
        const char *const statenames[] = {"STATE_NONE",  "STATE_CONNECTING", "STATE_CONNECTING_RE", "STATE_ACCEPTING", "STATE_CONNECTION_ESTABLISHED",
                                          "STATE_CLOSED"};
        return statenames[state];
    }

   public:
    // used by eventcallback
    void handle_write();
    void handle_write_callback();
    void process();
    void wakeup_from(uint64_t id);
    void tick(uint64_t id);
    void stop(bool queue_reset);
    void cleanup();

   private:
    NetworkStack *stack;
    Context *context;
    Worker *localWorker;
    entity_addr_t peer_addr;
};
#endif