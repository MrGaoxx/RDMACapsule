#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdint.h>

#include <atomic>
#include <functional>

#include "RDMAStack.h"
#include "common/context.h"
#include "common/statistic.h"
#include "core/Infiniband.h"
#include "core/server.h"
extern Logger clientLogger;
extern LockedOriginalLoggerTerm<TimeRecords, TimeRecordTerm> clientTimeRecords;

class RDMAPingPongClient {
   public:
    RDMAPingPongClient(std::string& configFileName);
    void Init();
    Connection* Connect(const char* serverAddr);
    void SendBatches(Connection*);
    void SendOnce();
    void OnConnectionReadable(Connection*);
    void OnSendCompletion(Infiniband::MemoryManager::Chunk*);

   private:
    uint32_t kRequestSize = 32768;
    uint32_t kNumRequest = 8;

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
    char* data;
    // std::mutex lock_inflight;
    std::atomic<uint64_t> inflight_size = 0;
};

RDMAPingPongClient::RDMAPingPongClient(std::string& configFileName)
    : rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0) {
    send_call = std::bind(&RDMAPingPongClient::SendBatches, this, std::placeholders::_1);
    readable_callback = std::bind(&RDMAPingPongClient::OnConnectionReadable, this, std::placeholders::_1);
    server.conn_write_callback_p = &send_call;
    server.conn_read_callback_p = &readable_callback;
    clientLogger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::rdtsc()) + "client.log");
    data = new char[kRequestSize];
    kRequestSize = context->m_rdma_config_->m_request_size;
    kNumRequest = context->m_rdma_config_->m_request_num;
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

void RDMAPingPongClient::SendBatches(Connection*) {
    uint32_t iters = 0;
    server.set_txc_callback(server_addr, std::bind(&RDMAPingPongClient::OnSendCompletion, this, std::placeholders::_1));
    uint64_t inflight_threshold = (kNumRequest / 2) * static_cast<uint64_t>(kRequestSize);
    while (true) {
        uint64_t inflight_size_value;
        while ((inflight_size_value = inflight_size.load()) <= inflight_threshold) {
            uint64_t sending_data_size = (kNumRequest * static_cast<uint64_t>(kRequestSize) - inflight_size_value) / kRequestSize * kRequestSize;
            // std::cout << "sending data size" << sending_data_size << std::endl;
            std::vector<Infiniband::MemoryManager::Chunk*> buffers;
            GetBuffers(buffers, sending_data_size);
            BufferList bl;
            std::size_t buffer_index = 0;
            for (uint64_t sending_count = 0; sending_count < sending_data_size; sending_count += kRequestSize) {
                int remainingSize = kRequestSize;
                do {
                    kassert(buffer_index < buffers.size());
                    remainingSize -= buffers[buffer_index]->write(data, remainingSize);
                    Buffer buf(buffers[buffer_index]->buffer, buffers[buffer_index]->get_offset());
                    bl.Append(buf);
                    buffer_index++;
                } while (remainingSize);
            }
            uint64_t now = Cycles::get_soft_timestamp_us();
            // kassert(buffers.size() == kNumRequest);
            for (auto chunk : buffers) {
                clientTimeRecords.Add(TimeRecordTerm{chunk->log_id, TimeRecordType::APP_SEND_BEFORE, now});
            }
            inflight_size += sending_data_size;
            // std::cout << __func__ << " inflight size" << inflight_size.load() << std::endl;
            server.send(server_addr, bl);
            now = Cycles::get_soft_timestamp_us();
            for (auto chunk : buffers) {
                clientTimeRecords.Add(TimeRecordTerm{chunk->log_id, TimeRecordType::APP_SEND_AFTER, now});
            }
        }
    }
}

void RDMAPingPongClient::OnConnectionReadable(Connection*) { std::cout << __func__ << std::endl; }

void RDMAPingPongClient::OnSendCompletion(Infiniband::MemoryManager::Chunk* chunk) {
    std::lock_guard<std::mutex> lock(data_lock);
    clientTimeRecords.Add(TimeRecordTerm{chunk->log_id, TimeRecordType::SEND_CB, Cycles::get_soft_timestamp_us()});
    chunk->log_id++;
    // std::cout << __func__ << "removing inflight size" << chunk->get_offset() << std::endl;
    kassert(inflight_size.load() >= chunk->get_offset());
    inflight_size -= chunk->get_offset();
    // std::cout << __func__ << " inflight size" << inflight_size.load() << std::endl;

    // SendOnce();
    //  clientTimeRecords.Flush();
    //   uint64_t lat = chunk_timeinfos[chunk].send_completion_time - chunk_timeinfos[chunk].post_send_time;
    //    average_latency.Add(lat);
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