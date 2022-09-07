
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cerrno>
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
    void OnConnectionWriteable(Connection*);

    static const uint32_t kRequestSize = 32768;
    static const uint32_t kMaxNumRequest = 8;
    char recv_buffer[kRequestSize][kMaxNumRequest];
    uint8_t pos;

   private:
    bool listening;
    Config* rdma_config;
    Context* context;
    Server server;

    entity_addr_t server_addr;
    entity_addr_t client_addr;
    std::function<void(Connection*)> poll_call;
    std::function<void(Connection*)> conn_writeable_callback;
};

RDMAPingPongServer::RDMAPingPongServer(std::string& configFileName)
    : pos(0),
      listening(false),
      rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0) {
    poll_call = std::bind(&RDMAPingPongServer::Poll, this, std::placeholders::_1);
    conn_writeable_callback = std::bind(&RDMAPingPongServer::OnConnectionWriteable, this, std::placeholders::_1);
    server.conn_read_callback_p = &poll_call;
    server.conn_write_callback_p = &conn_writeable_callback;
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

    server_addr.set_family(AF_INET);
    sockaddr_in sa;
    inet_pton(AF_INET, rdma_config->m_ip_addr.c_str(), &sa.sin_addr);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(rdma_config->m_listen_port);
    server_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&sa));

    std::cout << "SERVER:: listening on the addr" << server_addr << std::endl;
    return server.bind(server_addr);
}
void RDMAPingPongServer::Poll(Connection*) {
    while (true) {
        server.drain();
        /*
        int rs = server.Read(server_addr, recv_buffer[pos], kRequestSize);
        if (likely(rs > 0)) {
            if (unlikely(rs != kRequestSize)) {
                std::cout << "!!! read the recv buffer of size:[" << rs << "] expected:[" << kRequestSize << "]" << std::endl;
            }
            std::cout << "read the recv buffer \n";
            pos = (pos + 1) % kMaxNumRequest;
        } else {
            if (rs != -EAGAIN && rs != -104) {
                std::cout << __func__ << " READ error:\t" << rs << "\t" << strerror(rs) << std::endl;
            }
        }
        */
    }
}
void RDMAPingPongServer::OnConnectionWriteable(Connection*) { std::cout << __func__ << std::endl; }

// std::ostream& operator<<(std::ostream& os, timespec& tv) { return os << "tv.tv_sec: " << tv.tv_sec << " tv.tv_nsec: " << tv.tv_nsec << std::endl; }

int main(int argc, char* argv[]) {
    Cycles::init();
    /*
    while (true) {
        std::cout << "==================================" << std::endl;
        test_rdtsc_time();
        test_gettimeofday_time();
        test_clocktime_time();
        test_rdtsc_rdtsc();
        test_gettimeofday_rdtsc();
        std::cout << "==================================" << std::endl;
        Cycles::sleep(1000000);
    }
    */

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
