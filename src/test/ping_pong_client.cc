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

extern Logger clientLogger;
extern OriginalLoggerTerm<TimeRecords, TimeRecordTerm> clientTimeRecords;

class RDMAPingPongClient {
   public:
    RDMAPingPongClient(std::string& configFileName);
    void Init();
    Connection* Connect(const char* serverAddr);
    void Send(Connection*);
    void OnConnectionReadable(Connection*);
    void OnSendCompletion(Infiniband::MemoryManager::Chunk*);

   private:
    uint32_t send_iterations = 1024;
    static const uint32_t kRequestSize = 128;
    static const uint32_t kNumRequest = 1;

    int GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size);

    std::function<void(Connection*)> send_call;
    std::function<void(Connection*)> readable_callback;
    Config* rdma_config;
    Context* context;
    Server server;
    Connection* conn;
    entity_addr_t server_addr;
    entity_addr_t client_addr;
    std::mutex data_lock;
    // Logger logger;
    //  AverageLoggerTerm<double> average_latency;
};

RDMAPingPongClient::RDMAPingPongClient(std::string& configFileName)
    : rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0)
// ,average_latency("average_latency", 10, &logger)
{
    send_call = std::bind(&RDMAPingPongClient::Send, this, std::placeholders::_1);
    readable_callback = std::bind(&RDMAPingPongClient::OnConnectionReadable, this, std::placeholders::_1);
    server.conn_write_callback_p = &send_call;
    server.conn_read_callback_p = &readable_callback;
    clientLogger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::rdtsc()) + "client.log");
    // logger.SetLoggerName(std::to_string(Cycles::rdtsc()) + "client.log");
}

void RDMAPingPongClient::Init() { server.start(); }
Connection* RDMAPingPongClient::Connect(const char* serverAddr) {
    client_addr.set_addr(rdma_config->m_ip_addr.c_str(), rdma_config->m_listen_port);
    server_addr.set_addr(serverAddr, rdma_config->m_listen_port);
    std::cout << typeid(this).name() << " : " << __func__ << server_addr << std::endl;
    conn = server.create_connect(server_addr);
    return conn;
}

int RDMAPingPongClient::GetBuffers(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers(buffers, size);
    return 0;
}

void RDMAPingPongClient::Send(Connection*) {
    // const char* prefix = "this is the test of iteration";
    char* data = new char(kRequestSize);
    // int size_prefix = strlen(prefix);

    uint32_t iters = 0;
    server.set_txc_callback(server_addr, std::bind(&RDMAPingPongClient::OnSendCompletion, this, std::placeholders::_1));
    // sleep(3600);
    while (iters < send_iterations) {
        std::vector<Infiniband::MemoryManager::Chunk*> buffers;
        for (uint32_t numRequests = 0; numRequests < kNumRequest; numRequests++) {
            GetBuffers(buffers, RDMAPingPongClient::kRequestSize);
        }
        BufferList bl;
        std::size_t buffer_index = 0;
        for (uint32_t numRequests = 0; numRequests < kNumRequest; numRequests++) {
            int remainingSize = kRequestSize;
            do {
                kassert(buffer_index < buffers.size());
                remainingSize -= buffers[buffer_index]->write(data, remainingSize);
                Buffer buf(buffers[buffer_index]->buffer, buffers[buffer_index]->get_offset());
                bl.Append(buf);
                buffer_index++;
            } while (remainingSize);
        }
        // uint64_t now = Cycles::rdtsc();
        uint64_t now = Cycles::get_soft_timestamp_us();
        for (auto chunk : buffers) {
            std::lock_guard<std::mutex>{data_lock};
            chunk->log_id++;
            std::cout << "current log id is : " << chunk->log_id << std::endl;
            clientTimeRecords.Add(TimeRecordTerm{chunk->log_id, TimeRecordType::APP_SEND_BEFORE, now});
        }
        server.Send(server_addr, bl);

        now = Cycles::get_soft_timestamp_us();
        for (auto chunk : buffers) {
            std::lock_guard<std::mutex>{data_lock};
            clientTimeRecords.Add(TimeRecordTerm{chunk->log_id, TimeRecordType::APP_SEND_AFTER, now});
        }
        iters++;
        std::cout << "finished send iteration: " << iters << std::endl;
        Cycles::sleep(1e6);
    }
}
void RDMAPingPongClient::OnConnectionReadable(Connection*) { std::cout << __func__ << std::endl; }

void RDMAPingPongClient::OnSendCompletion(Infiniband::MemoryManager::Chunk* chunk) {
    std::lock_guard<std::mutex>{data_lock};
    clientTimeRecords.Add(TimeRecordTerm{chunk->log_id, TimeRecordType::SEND_CB, Cycles::get_soft_timestamp_us()});
    // clientTimeRecords.Flush();
    //  uint64_t lat = chunk_timeinfos[chunk].send_completion_time - chunk_timeinfos[chunk].post_send_time;
    //   average_latency.Add(lat);
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