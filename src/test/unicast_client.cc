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
// uint64_t start_connect;

class RDMAUnicastClient {
   public:
    RDMAUnicastClient(std::string& configFileName, uint8_t num_replicas);
    void Init();
    Connection* Connect(const char* serverAddr, uint8_t index);
    void SendSmallRequests(Connection*);
    void SendBigRequests(Connection*);
    void OnConnectionReadable(Connection*);
    void OnSendCompletion(Infiniband::MemoryManager::Chunk*);
    void SetNumReplicas(uint8_t);

   private:
    uint64_t kRequestSize = 32768;
    uint64_t kNumRequest = 8;
    static const uint8_t kMaxRequestNum = 128;
    int GetBuffersBySize(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size);
    int GetBuffersByNum(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t num);

    std::function<void(Connection*)> send_call;
    std::function<void(Connection*)> readable_callback;
    Config* rdma_config;
    Context* context;
    Server server;
    //    Server server_repli1;
    //    Server server_repli2;
    static const uint32_t kUnicastMaxRecordTime = 8192;
    static const uint8_t kMaxNumReplicas = 4;
    uint8_t kNumReplicas;
    Connection* conn[kMaxNumReplicas];
    entity_addr_t server_addr[kMaxNumReplicas];
    entity_addr_t client_addr;
    std::mutex data_lock;
    char* data;
    // std::mutex lock_inflight;
    volatile uint64_t request_id[kMaxNumReplicas];
    std::atomic<uint64_t> inflight_size;

    struct RequestACKCounter {
        RequestACKCounter() = delete;
        RequestACKCounter(uint8_t num_replicas) : m_num_replicas(num_replicas) {
            for (int index = 0; index < kMaxRequestNum; index++) {
                request_acked_num[index] = 0;
            }
        };
        bool Increase(uint64_t request_id) {
            std::lock_guard<std::mutex>{lock};
            bool r;
            request_acked_num[request_id % kMaxRequestNum]++;
            if (request_acked_num[request_id % kMaxRequestNum] == m_num_replicas) {
                request_acked_num[request_id % kMaxRequestNum] = 0;
                r = true;
            } else {
                r = false;
            }
            return r;
        }
        std::mutex lock;
        volatile uint8_t request_acked_num[kMaxRequestNum];
        uint8_t m_num_replicas;
    } request_ack_counter;

    Logger unicast_logger;
    LockedOriginalLoggerTerm<TimeRecords, TimeRecordTerm> client_unicast_records;

    uint8_t num_ready = 0;
};

RDMAUnicastClient::RDMAUnicastClient(std::string& configFileName, uint8_t num_replicas)
    : rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      kNumReplicas(num_replicas),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0),
      request_ack_counter(num_replicas),
      client_unicast_records("RequestTimeRecord", kUnicastMaxRecordTime, &unicast_logger) {
    kRequestSize = context->m_rdma_config_->m_request_size;
    kNumRequest = context->m_rdma_config_->m_request_num;

    readable_callback = std::bind(&RDMAUnicastClient::OnConnectionReadable, this, std::placeholders::_1);
    server.client_conn_write_callback_p = &send_call;
    server.client_conn_read_callback_p = &readable_callback;
    clientLogger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::rdtsc()) + "client.log");
    unicast_logger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::rdtsc()) + "unicast_client.log");

    data = new char[kRequestSize];

    for (int replica_index = 0; replica_index < kNumReplicas; replica_index++) {
        server_addr[replica_index] = {entity_addr_t::type_t::TYPE_SERVER, 0};
        inflight_size.store(0);
        request_id[replica_index] = 1;
    }
    if (kRequestSize >= rdma_config->m_rdma_buffer_size_bytes_) {
        send_call = std::bind(&RDMAUnicastClient::SendBigRequests, this, std::placeholders::_1);
    } else {
        send_call = std::bind(&RDMAUnicastClient::SendSmallRequests, this, std::placeholders::_1);
    }
}

void RDMAUnicastClient::Init() { server.start(); }
Connection* RDMAUnicastClient::Connect(const char* serverAddr, uint8_t index) {
    kassert(index < kNumReplicas);
    client_addr.set_addr(rdma_config->m_ip_addr.c_str(), rdma_config->m_listen_port);
    server_addr[index].set_addr(serverAddr, rdma_config->m_listen_port);
    std::cout << typeid(this).name() << " : " << __func__ << server_addr[index] << std::endl;
    conn[index] = server.create_connect(server_addr[index]);
    return conn[index];
}

int RDMAUnicastClient::GetBuffersByNum(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t num) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers_by_num(buffers, num);
    return 0;
}
int RDMAUnicastClient::GetBuffersBySize(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers_by_size(buffers, size);
    return 0;
}

void RDMAUnicastClient::SendSmallRequests(Connection*) { /* this function will be called for kMaxNumReplicas times in (maybe) RDMA polling
                                                          * processing threads only one thread is used for RDMA polling processing now
                                                          */
    uint64_t now = Cycles::get_soft_timestamp_us();
    std::cout << "connecting time is "<< now-start_connect<<std::endl;

    cpu_set_t mask;                                     // cpu核的集合
    CPU_ZERO(&mask);                                    // 将集合置为空集
    CPU_SET(context->m_rdma_config_->m_cpu_id+2, &mask);  // 设置亲和力值

    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1)  // 设置线程cpu亲和力
    {
        std::cout << "warning: could not set CPU affinity, continuing...\n" << std::endl;
    }

    std::lock_guard<std::mutex>{data_lock};
    for (int replica_index = 0; replica_index < kNumReplicas; replica_index++) {
        server.set_txc_callback(server_addr[replica_index], std::bind(&RDMAUnicastClient::OnSendCompletion, this, std::placeholders::_1));
    }
    {
        num_ready++;
        if (num_ready < kNumReplicas) return;  // connections not ready
    }
    uint64_t inflight_threshold = (kNumRequest / 2) * static_cast<uint64_t>(kRequestSize);
    inflight_threshold = inflight_threshold > 8388608 ? 8388608 : inflight_threshold;
    while (true) {
        uint64_t inflight_size_value;
        while ((inflight_size_value = inflight_size.load()) <= inflight_threshold) {
#ifndef NDEBUG
            {
                auto consistency = request_id[0];
                for (int replica_index = 0; replica_index < kNumReplicas; replica_index++) {
                    if (unlikely(consistency != request_id[replica_index])) {
                        std::cout << "request id of replicas are inconsistent!" << std::endl;
                        abort();
                    }
                }
            }
#endif

            uint64_t sending_data_num = (kNumRequest * static_cast<uint64_t>(kRequestSize) - inflight_size_value) / kRequestSize;
            uint64_t sending_data_size = sending_data_num * kRequestSize;
            inflight_size += sending_data_size;
            client_unicast_records.Add(
                TimeRecordTerm{request_id[0] /*all the request id are the same*/, TimeRecordType::POST_SEND, Cycles::get_soft_timestamp_us()});
            for (uint8_t replica_index = 0; replica_index < kNumReplicas; replica_index++) {
                std::vector<Infiniband::MemoryManager::Chunk*> buffers;
                GetBuffersByNum(buffers, sending_data_num);
                std::size_t buffer_index = 0;
                uint64_t now = Cycles::get_soft_timestamp_us();
                for (auto& chunk : buffers) {
                    chunk->zero_fill(kRequestSize);
                    // chunk->replica_id = replica_index;
                    chunk->request_id = request_id[replica_index]++;
                }
                server.send(server_addr[replica_index], buffers);
            }
        }
    }
}

void RDMAUnicastClient::SendBigRequests(Connection*) {
    uint64_t now = Cycles::get_soft_timestamp_us();
    std::cout << "connecting time is "<< now-start_connect<<std::endl;

    cpu_set_t mask;                                     // cpu核的集合
    CPU_ZERO(&mask);                                    // 将集合置为空集
    CPU_SET(context->m_rdma_config_->m_cpu_id+2, &mask);  // 设置亲和力值

    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) == -1)  // 设置线程cpu亲和力
    {
        std::cout << "warning: could not set CPU affinity, continuing...\n" << std::endl;
    }
    
    std::lock_guard<std::mutex>{data_lock};
    for (int replica_index = 0; replica_index < kNumReplicas; replica_index++) {
        server.set_txc_callback(server_addr[replica_index], std::bind(&RDMAUnicastClient::OnSendCompletion, this, std::placeholders::_1));
    }
    {
        num_ready++;
        if (num_ready < kNumReplicas) return;  // connections not ready
    }
    uint64_t max_qp_inflight_size = 128*65536/2;
    uint64_t inflight_threshold = (kNumRequest / 2) * static_cast<uint64_t>(kRequestSize);
    inflight_threshold = inflight_threshold > max_qp_inflight_size ? max_qp_inflight_size : inflight_threshold;
    uint64_t max_inflight_request_size = kNumRequest * static_cast<uint64_t>(kRequestSize);
    max_inflight_request_size = max_inflight_request_size > max_qp_inflight_size ? max_qp_inflight_size : max_inflight_request_size;

    while (true) {
        uint64_t inflight_size_value;
        while ((inflight_size_value = inflight_size.load()) <= inflight_threshold) {
            uint64_t sending_data_size = (max_inflight_request_size - inflight_size_value) / kRequestSize * kRequestSize;
            inflight_size += sending_data_size;
            for (int request_index = 0; request_index < sending_data_size / kRequestSize; request_index++) {
                client_unicast_records.Add(
                    TimeRecordTerm{request_id[0] /*all the request id are the same*/, TimeRecordType::POST_SEND, Cycles::get_soft_timestamp_us()});
#ifndef NDEBUG
                {
                    auto consistency = request_id[0];
                    for (int replica_index = 0; replica_index < kNumReplicas; replica_index++) {
                        if (unlikely(consistency != request_id[replica_index])) {
                            std::cout << "request id of replicas are inconsistent!" << std::endl;
                            abort();
                        }
                    }
                }
#endif
                for (uint8_t replica_index = 0; replica_index < kNumReplicas; replica_index++) {
                    std::vector<Infiniband::MemoryManager::Chunk*> buffers;
                    GetBuffersBySize(buffers, kRequestSize);
                    std::size_t buffer_index = 0;
                    int remainingSize = kRequestSize;
                    do {
                        kassert(buffer_index < buffers.size());
                        remainingSize -= buffers[buffer_index]->zero_fill(remainingSize);
                        buffers[buffer_index]->request_id = 0;
                        buffer_index++;

                    } while (remainingSize);
                    kassert(buffer_index == buffers.size());
                    // buffers.back()->replica_id = replica_index;
                    buffers[buffers.size() - 1]->request_id = request_id[replica_index]++;
                    server.send(server_addr[replica_index], buffers);
                }
            }
        }
    }
}

void RDMAUnicastClient::OnConnectionReadable(Connection*) { std::cout << __func__ << std::endl; }

void RDMAUnicastClient::OnSendCompletion(Infiniband::MemoryManager::Chunk* chunk) {
    std::lock_guard<std::mutex> lock(data_lock);
    kassert(inflight_size.load() >= chunk->get_offset());
    // std::cout << "load finished" << request_id << std::endl;
    if (chunk->request_id != 0) {
        // std::cout << "complete request_id" << request_id << std::endl;
        auto request_id = chunk->request_id;
        // auto replica_id = chunk->replica_id;
        bool r = request_ack_counter.Increase(request_id);
        if (r) {
            client_unicast_records.Add(TimeRecordTerm{request_id, TimeRecordType::POLLED_CQE, Cycles::get_soft_timestamp_us()});
            inflight_size -= kRequestSize;
        }
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
    RDMAUnicastClient rdmaClient(configFileName, std::stoi(argv[2]));
    rdmaClient.Init();
    for (uint8_t index = 0; index < std::stoi(argv[2]); index++) {
        rdmaClient.Connect(argv[3 + index], index);
    }
    start_connect = Cycles::get_soft_timestamp_us();
    sleep(10000);
    return 0;
}