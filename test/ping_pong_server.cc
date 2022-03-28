#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "RDMAStack.h"
#include "common.h"
#include "common/context.h"
#include "core/Infiniband.h"
class RDMAPingPongServer {
   public:
    RDMAPingPongServer(std::string& configFileName);
    ~RDMAPingPongServer();
    void Init();
    int Listen(const char* serverPort);
    void Accept();
    void Poll();

    RDMAStack* get_rdma_stack() { return reinterpret_cast<RDMAStack*>(rdma_stack.get()); }
    ConnectedSocket* get_connected_socket() { return &connected_socket; }
    static const uint32_t kRequestSize = 32 * 1024;
    static const uint32_t kNumRequest = 8;
    char recv_buffer[kRequestSize][kNumRequest];
    uint8_t pos;

   private:
    RDMAConfig* rdma_config;
    Context* context;
    std::shared_ptr<NetworkStack> rdma_stack;

    bool is_listening;
    ConnectedSocket connected_socket;
    entity_addr_t client_addr;
    SocketOptions server_sockopts;
    ServerSocket* server_sock;
};

RDMAPingPongServer::RDMAPingPongServer(std::string& configFileName) : is_listening(false), pos(0) {
    rdma_config = new RDMAConfig(configFileName);
    rdma_config->m_op_threads_num_ = 1;
    context = new Context(rdma_config);
    rdma_stack = NetworkStack::create(context, "rdma");
    server_sock = new ServerSocket();
    // recv_buffer = reinterpret_cast<char*>(malloc(kRequestSize * kNumRequest));
}
RDMAPingPongServer::~RDMAPingPongServer() {
    delete rdma_config;
    delete context;
    // free(reinterpret_cast<void*>(recv_buffer));
}
void RDMAPingPongServer::Init() { rdma_stack->start(); }
int RDMAPingPongServer::Listen(const char* serverPort) {
    if (unlikely(is_listening)) {
        return -EBUSY;
    }
    Worker* worker = rdma_stack->get_worker(0);
    entity_addr_t server_addr(entity_addr_t::type_t::TYPE_SERVER, 0);  // nonce = 0
    server_addr.set_family(AF_INET);
    sockaddr_in sa;
    inet_pton(AF_INET, rdma_config->m_ipAddr.c_str(), &sa.sin_addr);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(serverPort));
    server_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&sa));

    server_sockopts.connect_bind_addr = server_addr;
    server_sockopts.nodelay = true;
    server_sockopts.nonblock = true;
    server_sockopts.priority = IPTOS_CLASS_CS3;
    server_sockopts.rcbuf_size = 32 * 1024;

    std::cout << "SERVER:: listening on the addr" << server_addr << std::endl;
    int error = worker->listen(server_addr, server_sockopts, server_sock);
    is_listening = ~error;
    return error;
}
void RDMAPingPongServer::Accept() {
    int rs = 0;
    do {
        rs = server_sock->accept(&connected_socket, server_sockopts, &client_addr, rdma_stack->get_worker(0));
        if (unlikely(rs && rs != -11)) {
            std::cout << " accept socket failed, errno is " << rs << std::endl;
            abort();
        }
    } while (rs != 0);
    std::cout << "==========> accept socket successful " << std::endl;
    return;
};
void RDMAPingPongServer::Poll() {
    while (true) {
        int rs = connected_socket.read(recv_buffer[pos], kRequestSize);
        if (unlikely(rs != -EAGAIN)) {
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
    std::cout << "The listening port is: " << atoi(argv[2]) << std::endl;
    std::string configFileName(argv[1]);
    RDMAPingPongServer server(configFileName);
    server.Init();
    int error = server.Listen(argv[2]);
    if (unlikely(error)) {
        std::cout << "worker cannot listen socket on addr" << cpp_strerror(error) << std::endl;
        return 1;
    } else {
        std::cout << "==========> listening socket succeeded" << std::endl;
    };
    server.Accept();
    // server.Poll();
    return 0;
}
