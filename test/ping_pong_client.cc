#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdint.h>

#include "RDMAStack.h"
#include "common/context.h"
#include "core/Infiniband.h"
class RDMAPingPongClient {
   public:
    RDMAPingPongClient(std::string& configFileName);
    void Init();
    ConnectedSocket* Connect(const char* serverIPAddr, const char* serverPort);
    void Send(uint32_t iterations);

   private:
    static const uint32_t kRequestSize = 32767 * 1024;
    static const uint32_t kNumRequest = 8;

    RDMAStack* get_rdma_stack() { return reinterpret_cast<RDMAStack*>(rdma_stack.get()); }
    int GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size);
    RDMAConfig* rdma_config;
    Context* context;
    std::shared_ptr<NetworkStack> rdma_stack;
    ConnectedSocket connected_socket;
};

RDMAPingPongClient::RDMAPingPongClient(std::string& configFileName) {
    rdma_config = new RDMAConfig(configFileName);
    rdma_config->m_op_threads_num_ = 1;
    context = new Context(rdma_config);
    rdma_stack = NetworkStack::create(context, "rdma");
}

void RDMAPingPongClient::Init() { rdma_stack->start(); }

ConnectedSocket* RDMAPingPongClient::Connect(const char* serverIPAddr, const char* serverPort) {
    // use the first worker to connect
    Worker* worker = rdma_stack->get_worker(0);

    entity_addr_t server_addr(entity_addr_t::type_t::TYPE_SERVER, 0);  // nonce = 0
    server_addr.set_family(AF_INET);
    // sockaddr sa;
    sockaddr_in server_sock_addr;
    inet_pton(AF_INET, serverIPAddr, &server_sock_addr.sin_addr);
    server_sock_addr.sin_family = AF_INET;
    server_sock_addr.sin_port = htons(atoi(serverPort));
    // sa.sin_zero = ;
    server_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&server_sock_addr));

    entity_addr_t client_addr(entity_addr_t::type_t::TYPE_SERVER, 0);  // nonce = 0
    client_addr.set_family(AF_INET);
    // sockaddr sa;
    sockaddr_in client_socket_addr;
    inet_pton(AF_INET, rdma_config->m_ipAddr.c_str(), &client_socket_addr.sin_addr);
    client_socket_addr.sin_family = AF_INET;
    client_socket_addr.sin_port = htons(atoi(serverPort));
    // sa.sin_zero = ;
    server_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&client_socket_addr));

    SocketOptions opts;
    {
        opts.connect_bind_addr = server_addr;
        opts.nodelay = true;
        opts.nonblock = true;
        opts.priority = IPTOS_CLASS_CS3;
        opts.rcbuf_size = 32 * 1024;
    }

    ServerSocket* sock;

    std::cout << "SERVER:: listening on the addr" << server_addr << std::endl;
    int error;

    if (error = worker->connect(server_addr, opts, &connected_socket)) {
        std::cout << "worker cannot connect addr" << server_addr << cpp_strerror(error) << std::endl;
        return &connected_socket;
    };
    return &connected_socket;
}

int RDMAPingPongClient::GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size) {
    Infiniband::MemoryManager* memoryManager = get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers(buffers, size);
    return 0;
}

void RDMAPingPongClient::Send(uint32_t iterations) {
    const char* prefix = "this is the test of iteration";
    int size_prefix = strlen(prefix);

    int i = 0;
    while (i < iterations) {
        std::vector<Infiniband::MemoryManager::Chunk*> buffers;
        GetBuffers(buffers, RDMAPingPongClient::kRequestSize * RDMAPingPongClient::kNumRequest);
        kassert(buffers.size() == RDMAPingPongClient::kNumRequest);
        BufferList bl;
        for (auto& chunk : buffers) {
            memcpy(reinterpret_cast<void*>(chunk->buffer), reinterpret_cast<void*>(const_cast<char*>(prefix)), size_prefix);
            *(chunk->buffer + size_prefix) = i;
            i++;
            bl.Append(Buffer(chunk->buffer, chunk->bytes));
        }

        connected_socket.send(bl, false);
    }
}

int main(int argc, char** argv) {
    std::string configFileName(argv[1]);
    RDMAPingPongClient rdmaClient(configFileName);
    rdmaClient.Init();
    rdmaClient.Connect(argv[2], argv[3]);
    rdmaClient.Send(1000000);
    return 0;
}