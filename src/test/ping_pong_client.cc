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
// uint64_t now = Cycles::get_soft_timestamp_us();
// uint64_t start_connect;

class RDMAPingPongClient {
   public:
    RDMAPingPongClient(std::string& configFileName);
    void Init();
    Connection* Connect(const char* serverAddr);
    void SendSmallRequests(Connection*);
    void SendBigRequests(Connection*);
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
    Logger m_logger_rpc;

    LockedOriginalLoggerTerm<TimeRecords, TimeRecordTerm> m_client_loggger_records;
    TimeAverageLoggerTerm<uint64_t> m_client_loggger_records_rpc;
};

RDMAPingPongClient::RDMAPingPongClient(std::string& configFileName)
    : rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0),
      m_client_loggger_records("RequestTimeRecord", kClientRequestMaxRecordTime, &m_client_logger),
      m_client_loggger_records_rpc("TX_BW", TX_LOG_INTERVAL, &m_logger_rpc) {
    kRequestSize = context->m_rdma_config_->m_request_size;
    kNumRequest = context->m_rdma_config_->m_request_num;
    if (kRequestSize > rdma_config->m_rdma_buffer_size_bytes_) {
        send_call = std::bind(&RDMAPingPongClient::SendBigRequests, this, std::placeholders::_1);
    } else {
        send_call = std::bind(&RDMAPingPongClient::SendSmallRequests, this, std::placeholders::_1);
    }
    readable_callback = std::bind(&RDMAPingPongClient::OnConnectionReadable, this, std::placeholders::_1);
    server.client_conn_write_callback_p = &send_call;
    server.client_conn_read_callback_p = &readable_callback;
    // clientLogger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::get_soft_timestamp_us()) + "client.log");
    m_client_logger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::get_soft_timestamp_us()) + "client_request.log");
    m_logger_rpc.SetLoggerName("/dev/shm/" + std::to_string(Cycles::get_soft_timestamp_us()) + "client_rpc_throughput.log");
    data = new char[kRequestSize];
}

void RDMAPingPongClient::Init() { 
    server.start(); 
    m_client_loggger_records_rpc.Start();
}

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
    uint64_t now = Cycles::get_soft_timestamp_us();
    std::cout << "connecting time is "<< now-start_connect<<std::endl;

    cpu_set_t mask;                                     // cpu核的集合
    CPU_ZERO(&mask);                                    // 将集合置为空集
    CPU_SET(context->m_rdma_config_->m_cpu_id+2, &mask);  // 设置亲和力值

    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1)  // 设置线程cpu亲和力
    {
        std::cout << "warning: could not set CPU affinity, continuing...\n" << std::endl;
    }

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
                m_client_loggger_records.Add(TimeRecordTerm{m_request_id, TimeRecordType::POST_SEND, now});
                chunk->request_id = m_request_id++;
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
    uint64_t now = Cycles::get_soft_timestamp_us();
    std::cout << "connecting time is "<< now-start_connect<<std::endl;
    server.set_txc_callback(server_addr, std::bind(&RDMAPingPongClient::OnSendCompletion, this, std::placeholders::_1));

    cpu_set_t mask;                                     // cpu核的集合
    CPU_ZERO(&mask);                                    // 将集合置为空集
    CPU_SET(context->m_rdma_config_->m_cpu_id+2, &mask);  // 设置亲和力值

    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1)  // 设置线程cpu亲和力
    {
        std::cout << "warning: could not set CPU affinity, continuing...\n" << std::endl;
    }

    uint64_t inflight_threshold = (kNumRequest / 2) * static_cast<uint64_t>(kRequestSize);
    while (true) {
        uint64_t inflight_size_value;
        while ((inflight_size_value = inflight_size.load()) <= inflight_threshold) {
            uint64_t sending_data_size = (kNumRequest * static_cast<uint64_t>(kRequestSize) - inflight_size_value) / kRequestSize * kRequestSize;
            inflight_size += sending_data_size;
            for (int request_index = 0; request_index < sending_data_size / kRequestSize; request_index++) {
                // std::cout << "sending data size" << sending_data_size << std::endl;
                std::vector<Infiniband::MemoryManager::Chunk*> buffers;
                GetBuffersBySize(buffers, kRequestSize);
                // BufferList bl;
                std::size_t buffer_index = 0;
                int remainingSize = kRequestSize;
                do {
                    kassert(buffer_index < buffers.size());
                    remainingSize -= buffers[buffer_index]->zero_fill(remainingSize);
                    buffers[buffer_index]->request_id = 0;
                    // Buffer buf(buffers[buffer_index]->buffer, buffers[buffer_index]->get_offset());
                    //  bl.Append(buf);
                    buffer_index++;

                } while (remainingSize);
                kassert(buffer_index == buffers.size());
                uint64_t now = Cycles::get_soft_timestamp_us();
                m_client_loggger_records.Add(TimeRecordTerm{m_request_id, TimeRecordType::POST_SEND, now});
                buffers.back()->request_id = m_request_id++;
                server.send(server_addr, buffers);
            }
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
        m_client_loggger_records_rpc.Add(kRequestSize);
    }
    // clientTimeRecords.Add(TimeRecordTerm{chunk->my_log_id, TimeRecordType::SEND_CB, Cycles::get_soft_timestamp_us()});
    // std::cout << __func__ << "removing inflight size" << chunk->get_offset() << std::endl;

    // std::cout << __func__ << " inflight size" << inflight_size.load() << std::endl;

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
    // start_connect = Cycles::get_soft_timestamp_us();
    sleep(10000);
    return 0;
}