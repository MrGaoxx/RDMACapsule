
#include <ifaddrs.h>
#include <linux/errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <functional>

#include "RDMAStack.h"
#include "common.h"
#include "common/context.h"
#include "core/Infiniband.h"
#include "core/server.h"
class RDMAPingPongServer {
   public:
    RDMAPingPongServer(std::string& configFileName);
    ~RDMAPingPongServer();

    void Init();
    int Listen();
    void Poll(Connection*);

    static const uint32_t kRequestSize = 32 * 1024;
    static const uint32_t kNumRequest = 8;
    char recv_buffer[kRequestSize][kNumRequest];
    uint8_t pos;

   private:
    bool listening;
    Config* rdma_config;
    Context* context;
    Server server;

    entity_addr_t server_addr;
    entity_addr_t client_addr;
    std::function<void(Connection*)> poll_call;
};

RDMAPingPongServer::RDMAPingPongServer(std::string& configFileName)
    : listening(false),
      rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      pos(0),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0) {
    poll_call = std::bind(&RDMAPingPongServer::Poll, this, std::placeholders::_1);
    server.conn_read_callback = &poll_call;
}
RDMAPingPongServer::~RDMAPingPongServer() {
    delete rdma_config;
    delete context;
}
void RDMAPingPongServer::Init() { server.start(); }
int RDMAPingPongServer::Listen() {
    if (unlikely(listening)) {
        return -EBUSY;
    }
    server_addr.set_addr(rdma_config->m_ip_addr.c_str(), rdma_config->m_listen_port);
    std::cout << "SERVER:: listening on the addr" << server_addr << std::endl;
    return server.bind(server_addr);
}
void RDMAPingPongServer::Poll() {
    while (true) {
        int rs = server.Read(server_addr, recv_buffer[pos], kRequestSize);
        if (likely(rs <= 0)) {
            if (rs != -EAGAIN && rs != -104) {
                std::cout << __func__ << " READ error:\t" << rs << "\t" << strerror(rs) << std::endl;
            }
        } else {
            if (unlikely(rs != kRequestSize)) {
                std::cout << "!!! read the recv buffer of size:[" << rs << "] expected:[" << kRequestSize << "]" << std::endl;
            }
            std::cout << "read the recv buffer \n";
            for (auto& i : recv_buffer[pos]) {
                std::cout << i << " ";
            }
            std::cout << std::endl;
            pos = (pos + 1) % kNumRequest;
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "The filename of configuration file is: " << std::string(argv[1]) << std::endl;
    std::string configFileName(argv[1]);
    RDMAPingPongServer server(configFileName);
    server.Init();
    int error = server.Listen();
    if (unlikely(error)) {
        std::cout << "worker cannot listen socket on addr" << cpp_strerror(error) << std::endl;
        return 1;
    } else {
        std::cout << "==========> listening socket succeeded" << std::endl;
    };
    sleep(100000);
    return 0;
}
