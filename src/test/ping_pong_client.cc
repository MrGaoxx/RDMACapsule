#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdint.h>

#include <functional>

#include "RDMAStack.h"
#include "common/context.h"
#include "common/statistic.h"
#include "core/Infiniband.h"
#include "core/server.h"
struct timeinfo {
    uint64_t app_send_time;
    uint64_t post_send_time;
    uint64_t send_completion_time;
};

class RDMAPingPongClient {
   public:
    RDMAPingPongClient(std::string& configFileName);
    void Init();
    Connection* Connect(const char* serverAddr);
    void Send(Connection*);
    int OnSendCompletion(Infiniband::MemoryManager::Chunk*);

   private:
    uint32_t send_iterations = 1024;
    static const uint32_t kRequestSize = 32 * 1024;
    static const uint32_t kNumRequest = 8;

    int GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size);

    std::function<void(Connection*)> send_call;
    Config* rdma_config;
    Context* context;
    Server server;
    Connection* conn;
    entity_addr_t server_addr;
    entity_addr_t client_addr;
    std::unordered_map<Infiniband::MemoryManager::Chunk*, timeinfo> chunk_timeinfos;
    std::mutex data_lock;
    Logger logger;
    AverageLoggerTerm<double> average_latency;
};

RDMAPingPongClient::RDMAPingPongClient(std::string& configFileName)
    : rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0),
      average_latency("average_latency", 50, &logger) {
    send_call = std::bind(&RDMAPingPongClient::Send, this, std::placeholders::_1);
    server.conn_write_callback_p = &send_call;
    logger.SetLoggerName(std::to_string(Cycles::rdtsc()) + "client.log");
}

void RDMAPingPongClient::Init() { server.start(); }

Connection* RDMAPingPongClient::Connect(const char* serverAddr) {
    client_addr.set_addr(rdma_config->m_ip_addr.c_str(), rdma_config->m_listen_port);
    server_addr.set_addr(serverAddr, rdma_config->m_listen_port);
    std::cout << typeid(this).name() << " : " << __func__ << server_addr << std::endl;
    conn == server.create_connect(server_addr);
    return conn;
}

int RDMAPingPongClient::GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers(buffers, size);
    return 0;
}

void RDMAPingPongClient::Send(Connection*) {
    const char* prefix = "this is the test of iteration";
    int size_prefix = strlen(prefix);

    int i = 0;
    server.set_txc_callback(server_addr, std::bind(&RDMAPingPongClient::OnSendCompletion, this, std::placeholders::_1));
    // sleep(3600);
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
        uint64_t now = Cycles::rdtsc();
        for (auto& chunk : buffers) {
            std::lock_guard<std::mutex>{data_lock};
            chunk_timeinfos.insert(std::make_pair(chunk, timeinfo{now}));
        }
        server.Send(server_addr, bl);
    }
}

int RDMAPingPongClient::OnSendCompletion(Infiniband::MemoryManager::Chunk* chunk) {
    std::lock_guard<std::mutex>{data_lock};
    chunk_timeinfos[chunk].send_completion_time = Cycles::rdtsc();
    double lat = Cycles::to_microseconds(chunk_timeinfos[chunk].send_completion_time - chunk_timeinfos[chunk].app_send_time);
    average_latency.Add(lat);
}

int main(int argc, char* argv[]) {
    Cycles::init();
    std::string configFileName(argv[1]);
    RDMAPingPongClient rdmaClient(configFileName);
    rdmaClient.Init();
    rdmaClient.Connect(argv[2]);
    sleep(10000);
    return 0;
}