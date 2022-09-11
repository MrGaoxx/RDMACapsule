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
    void SendSmallRequests(Connection*);
    void SendBigRequests(Connection*);
    void SendOnce();
    void OnConnectionReadable(Connection*);
    void OnSendCompletion(Infiniband::MemoryManager::Chunk*);

   private:
    uint32_t kRequestSize = 32768;
    uint32_t kNumRequest = 8;

    int GetBuffersBySize(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size);
    int GetBuffersByNum(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t num);

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
    uint64_t m_request_id = 0;

    static const uint32_t kClientRequestMaxRecordTime = 8192;
    Logger m_client_logger;
    LockedOriginalLoggerTerm<TimeRecords, TimeRecordTerm> m_client_loggger_records;
};

RDMAPingPongClient::RDMAPingPongClient(std::string& configFileName)
    : rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0),
      m_client_loggger_records("RequestTimeRecord", kClientRequestMaxRecordTime, &m_client_logger) {
    if (kRequestSize > rdma_config->m_rdma_buffer_size_bytes_) {
        send_call = std::bind(&RDMAPingPongClient::SendBigRequests, this, std::placeholders::_1);
    } else {
        send_call = std::bind(&RDMAPingPongClient::SendSmallRequests, this, std::placeholders::_1);
    }
    readable_callback = std::bind(&RDMAPingPongClient::OnConnectionReadable, this, std::placeholders::_1);
    server.conn_write_callback_p = &send_call;
    server.conn_read_callback_p = &readable_callback;
    clientLogger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::get_soft_timestamp_us()) + "client.log");
    m_client_logger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::get_soft_timestamp_us()) + "client_request.log");
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

int RDMAPingPongClient::GetBuffersByNum(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t num) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers_by_num(buffers, num);
    return 0;
}
int RDMAPingPongClient::GetBuffersBySize(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers_by_size(buffers, size);
    return 0;
}

void RDMAPingPongClient::SendSmallRequests(Connection*) {
    uint32_t iters = 0;
    server.set_txc_callback(server_addr, std::bind(&RDMAPingPongClient::OnSendCompletion, this, std::placeholders::_1));
    uint64_t inflight_threshold = (kNumRequest / 2) * static_cast<uint64_t>(kRequestSize);
    while (true) {
        uint64_t inflight_size_value;
        while ((inflight_size_value = inflight_size.load()) <= inflight_threshold) {
            uint64_t sending_data_num = (kNumRequest * static_cast<uint64_t>(kRequestSize) - inflight_size_value) / kRequestSize;
            uint64_t sending_data_size = sending_data_num * kRequestSize;
            inflight_size += sending_data_size;
            // std::cout << "sending data size" << sending_data_size << std::endl;
            std::vector<Infiniband::MemoryManager::Chunk*> buffers;
            GetBuffersByNum(buffers, sending_data_num);
            // BufferList bl;
            std::size_t buffer_index = 0;

            uint64_t now = Cycles::get_soft_timestamp_us();
            for (auto& chunk : buffers) {
                chunk->zero_fill(kRequestSize);
                chunk->request_id = m_request_id++;
                m_client_loggger_records.Add(TimeRecordTerm{chunk->request_id, TimeRecordType::POST_SEND, now});
            }
            server.send(server_addr, buffers);
            /*
            now = Cycles::get_soft_timestamp_us();
            for (auto chunk : buffers) {
                clientTimeRecords.Add(TimeRecordTerm{chunk->my_log_id, TimeRecordType::APP_SEND_AFTER, now});
            }
            */
        }
    }
}

void RDMAPingPongClient::SendBigRequests(Connection*) {
    uint32_t iters = 0;
    server.set_txc_callback(server_addr, std::bind(&RDMAPingPongClient::OnSendCompletion, this, std::placeholders::_1));
    uint64_t inflight_threshold = (kNumRequest / 2) * static_cast<uint64_t>(kRequestSize);
    while (true) {
        uint64_t inflight_size_value;
        while ((inflight_size_value = inflight_size.load()) <= inflight_threshold) {
            uint64_t sending_data_size = (kNumRequest * static_cast<uint64_t>(kRequestSize) - inflight_size_value) / kRequestSize * kRequestSize;
            inflight_size += sending_data_size;
            // std::cout << "sending data size" << sending_data_size << std::endl;
            std::vector<Infiniband::MemoryManager::Chunk*> buffers;
            GetBuffersBySize(buffers, sending_data_size);
            // BufferList bl;
            std::size_t buffer_index = 0;
            int remainingSize = sending_data_size;

            do {
                kassert(buffer_index < buffers.size());
                remainingSize -= buffers[buffer_index]->zero_fill(remainingSize);
                buffers[buffer_index]->request_id = 0;
                // Buffer buf(buffers[buffer_index]->buffer, buffers[buffer_index]->get_offset());
                //  bl.Append(buf);
                buffer_index++;

            } while (remainingSize);
            kassert(buffer_index == buffers.size());
            buffers.back()->request_id = m_request_id++;
            uint64_t now = Cycles::get_soft_timestamp_us();
            m_client_loggger_records.Add(TimeRecordTerm{buffers.back()->request_id, TimeRecordType::POST_SEND, now});
            server.send(server_addr, buffers);
        }
    }
}

void RDMAPingPongClient::OnConnectionReadable(Connection*) { std::cout << __func__ << std::endl; }

void RDMAPingPongClient::OnSendCompletion(Infiniband::MemoryManager::Chunk* chunk) {
    std::lock_guard<std::mutex> lock(data_lock);
    kassert(inflight_size.load() >= chunk->get_offset());
    inflight_size -= chunk->get_offset();
    uint64_t now = Cycles::get_soft_timestamp_us();
    if (chunk->request_id != 0) {
        m_client_loggger_records.Add(TimeRecordTerm{chunk->request_id, TimeRecordType::POLLED_CQE, now});
    }
    // clientTimeRecords.Add(TimeRecordTerm{chunk->my_log_id, TimeRecordType::SEND_CB, Cycles::get_soft_timestamp_us()});
    // std::cout << __func__ << "removing inflight size" << chunk->get_offset() << std::endl;

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