#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdint.h>

#include <functional>

#include "RDMAStack.h"
#include "common/context.h"
#include "core/Infiniband.h"
#include "core/server.h"
class RDMAPingPongClient {
   public:
    RDMAPingPongClient(std::string& configFileName);
    void Init();
    Connection* Connect(const char* serverAddr);
    void Send();

   private:
    uint32_t send_iterations = 10000;
    static const uint32_t kRequestSize = 32 * 1024;
    static const uint32_t kNumRequest = 8;

    int GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size);

    std::function<void(void)> send_call;
    RDMAConfig* rdma_config;
    Context* context;
    Server server;
    Connection* conn;
    entity_addr_t server_addr;
    entity_addr_t client_addr;
};

RDMAPingPongClient::RDMAPingPongClient(std::string& configFileName)
    : rdma_config(new RDMAConfig(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0) {
    send_call = std::bind(&RDMAPingPongClient::Send, this);
    server.conn_write_callback = &send_call;
}

void RDMAPingPongClient::Init() { server.start(); }

Connection* RDMAPingPongClient::Connect(const char* serverAddr) {
    client_addr.set_family(AF_INET);
    sockaddr_in client_socket_addr;
    inet_pton(AF_INET, rdma_config->m_ip_addr.c_str(), &client_socket_addr.sin_addr);
    client_socket_addr.sin_family = AF_INET;
    client_socket_addr.sin_port = rdma_config->m_listen_port;
    client_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&client_socket_addr));

    server_addr.set_family(AF_INET);
    sockaddr_in server_sock_addr;
    inet_pton(AF_INET, serverAddr, &server_sock_addr.sin_addr);
    server_sock_addr.sin_family = AF_INET;
    server_sock_addr.sin_port = rdma_config->m_listen_port;
    server_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&server_sock_addr));

    std::cout << typeid(this).name() << " : " << __func__ << server_addr << std::endl;
    conn == server.create_connect(server_addr);
    return conn;
}

int RDMAPingPongClient::GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers(buffers, size);
    return 0;
}

void RDMAPingPongClient::Send() {
    const char* prefix = "this is the test of iteration";
    int size_prefix = strlen(prefix);

    int i = 0;
    while (i < send_iterations) {
        std::vector<Infiniband::MemoryManager::Chunk*> buffers;
        GetBuffers(buffers, RDMAPingPongClient::kRequestSize * RDMAPingPongClient::kNumRequest);
        kassert(buffers.size() == RDMAPingPongClient::kNumRequest);
        BufferList bl;
        for (auto& chunk : buffers) {
            for (int j = 0; j < kRequestSize / (size_prefix + sizeof(i)); j++) {
                chunk->write(const_cast<char*>(prefix), size_prefix);
                chunk->write(reinterpret_cast<char*>(&i), sizeof(i));
            }
            i++;
            Buffer buf(chunk->buffer, chunk->bytes);
            bl.Append(buf);
        }

        server.Send(server_addr, bl);
    }
}

int main(int argc, char* argv[]) {
    std::string configFileName(argv[1]);
    RDMAPingPongClient rdmaClient(configFileName);
    rdmaClient.Init();
    rdmaClient.Connect(argv[2]);
    sleep(10000);
    return 0;
}