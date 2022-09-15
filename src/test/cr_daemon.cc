
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
#include "common/statistic.h"

#define ACK_SIZE 62

class ChainReplicationClient {
   public:
    ChainReplicationClient(std::string& configFileName);
    void Init();
    Connection* Connect(const char* serverAddr);
    void SendRequests(uint32_t sending_reqesut_size);
    void OnConnectionReadable(Connection*);
    void OnSendCompletion(Infiniband::MemoryManager::Chunk*);
    void ReadyToSend(Connection*);
    bool IsReady() { return ready; }
    std::function<void(Connection*)> send_call;

   private:
    uint32_t kRequestSize = 32768;
    uint32_t kNumRequest = 8;
    uint16_t role = 0;

    int GetBuffersBySize(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size);
    int GetBuffersByNum(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t num);

    
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
    bool ready;
};

ChainReplicationClient::ChainReplicationClient(std::string& configFileName)
    : rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0),
      m_client_loggger_records("RequestTimeRecord", kClientRequestMaxRecordTime, &m_client_logger),
      ready(false) {
    kRequestSize = context->m_rdma_config_->m_request_size;
    kNumRequest = context->m_rdma_config_->m_request_num;
    role = context->m_rdma_config_->m_cr_role;
    send_call = std::bind(&ChainReplicationClient::ReadyToSend, this, std::placeholders::_1);
    readable_callback = std::bind(&ChainReplicationClient::OnConnectionReadable, this, std::placeholders::_1);
    server.conn_write_callback_p = &send_call;
    server.conn_read_callback_p = &readable_callback;
    // clientLogger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::get_soft_timestamp_us()) + "client.log");
    m_client_logger.SetLoggerName("/dev/shm/" + std::to_string(Cycles::get_soft_timestamp_us()) + "client_request.log");
    data = new char[kRequestSize];
    // server.set_txc_callback(server_addr, std::bind(&ChainReplicationClient::OnSendCompletion, this, std::placeholders::_1));
}

void ChainReplicationClient::Init() { server.start(); }
Connection* ChainReplicationClient::Connect(const char* serverAddr) {
    client_addr.set_addr(rdma_config->m_ip_addr.c_str(), rdma_config->m_listen_port);
    server_addr.set_addr(serverAddr, rdma_config->m_listen_port);
    std::cout << typeid(this).name() << " : " << __func__ << server_addr << std::endl;
    conn = server.create_connect(server_addr);
    return conn;
}

int ChainReplicationClient::GetBuffersByNum(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t num) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers_by_num(buffers, num);
    return 0;
}
int ChainReplicationClient::GetBuffersBySize(std::vector<Infiniband::MemoryManager::Chunk*>& buffers, uint32_t size) {
    Infiniband::MemoryManager* memoryManager = server.get_rdma_stack()->get_infiniband_entity()->get_memory_manager();
    memoryManager->get_send_buffers_by_size(buffers, size);
    return 0;
}

void ChainReplicationClient::ReadyToSend(Connection*) { 
    ready = true; 
    if (role == 0){
        sleep(5);
        SendRequests(kRequestSize);
    }
}

void ChainReplicationClient::SendRequests(uint32_t sending_reqesut_size) {
    // std::cout << "sending data size" << sending_data_size << std::endl;
    server.set_txc_callback(server_addr, std::bind(&ChainReplicationClient::OnSendCompletion, this, std::placeholders::_1));
    std::vector<Infiniband::MemoryManager::Chunk*> buffers;
    GetBuffersBySize(buffers, sending_reqesut_size);
    int buffer_index = 0;
    do {
        kassert(buffer_index < buffers.size());
        sending_reqesut_size -= buffers[buffer_index]->zero_fill(sending_reqesut_size);
        buffers[buffer_index]->request_id = 0;
        // Buffer buf(buffers[buffer_index]->buffer, buffers[buffer_index]->get_offset());
        //  bl.Append(buf);
        buffer_index++;

    } while (sending_reqesut_size);
    kassert(buffer_index == buffers.size());
    buffers.back()->request_id = m_request_id++;
    uint64_t now = Cycles::get_soft_timestamp_us();
    m_client_loggger_records.Add(TimeRecordTerm{buffers.back()->request_id, TimeRecordType::POST_SEND, now});
    server.send(server_addr, buffers);
}

void ChainReplicationClient::OnConnectionReadable(Connection*) { std::cout << __func__ << std::endl; }

void ChainReplicationClient::OnSendCompletion(Infiniband::MemoryManager::Chunk* chunk) {
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

    //  clientTimeRecords.Flush();
    //   uint64_t lat = chunk_timeinfos[chunk].send_completion_time - chunk_timeinfos[chunk].post_send_time;
    //    average_latency.Add(lat);
}

class ChainReplicationServer {
   public:
    ChainReplicationServer(std::string& configFileName);
    ~ChainReplicationServer();

    void Init();
    int Listen();
    void Poll(Connection*);
    void OnConnectionWriteable(Connection*);
    void SetTransmitClient(ChainReplicationClient*);
    static const uint32_t kRequestSize = 32768;
    static const uint32_t kMaxNumRequest = 8;
    char recv_buffer[kRequestSize][kMaxNumRequest];
    uint8_t pos;

   private:
    uint16_t role = 0;
    bool listening;
    Config* rdma_config;
    Context* context;
    Server server;

    entity_addr_t server_addr;
    entity_addr_t client_addr;
    std::function<void(Connection*)> poll_call;
    std::function<void(Connection*)> conn_writeable_callback;
    ChainReplicationClient* client;
};

ChainReplicationServer::ChainReplicationServer(std::string& configFileName)
    : pos(0),
      listening(false),
      rdma_config(new Config(configFileName)),
      context(new Context(rdma_config)),
      server(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0) {
    poll_call = std::bind(&ChainReplicationServer::Poll, this, std::placeholders::_1);
    conn_writeable_callback = std::bind(&ChainReplicationServer::OnConnectionWriteable, this, std::placeholders::_1);
    server.conn_read_callback_p = &poll_call;
    server.conn_write_callback_p = &conn_writeable_callback;
    role = context->m_rdma_config_->m_cr_role;
}
ChainReplicationServer::~ChainReplicationServer() {
    delete rdma_config;
    delete context;
}
void ChainReplicationServer::Init() { server.start(); }
int ChainReplicationServer::Listen() {
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

void ChainReplicationServer::SetTransmitClient(ChainReplicationClient* crClient) {
    client = crClient;
}

void ChainReplicationServer::Poll(Connection*) {
    int read_len = 0;
    while (true) {
        int rs = server.read(server_addr, recv_buffer[pos], kRequestSize);
        if (likely(rs > 0)) {
            read_len += rs;
            if (likely(read_len == kRequestSize)) {
                pos = (pos + 1) % kMaxNumRequest;
                kassert(client->IsReady());
                if (role == 1){
                    client->SendRequests(kRequestSize);
                }
                else if (role == 2){
                    client->SendRequests(ACK_SIZE);
                }
                
                read_len = 0;
            }
        } else {
            if (rs != -EAGAIN && rs != -104) {
                std::cout << __func__ << " READ error:\t" << rs << "\t" << strerror(rs) << std::endl;
                abort();
            }
        }
    }
}
void ChainReplicationServer::OnConnectionWriteable(Connection*) { std::cout << __func__ << std::endl; }


int main(int argc, char* argv[]) {
    Cycles::init();

    std::cout << "The filename of configuration file is: " << std::string(argv[1]) << std::endl;

    std::string configFileName(argv[1]);
    ChainReplicationServer server(configFileName);
    ChainReplicationClient client(configFileName);
    server.Init();
    int error = server.Listen();
    server.SetTransmitClient(&client);

    if (unlikely(error)) {
        std::cout << "worker cannot listen socket on addr" << cpp_strerror(error) << std::endl;
        return 1;
    } else {
        std::cout << "==========> listening socket succeeded" << std::endl;
    };

    client.Init();
    sleep(10);
    client.Connect(argv[2]);

    sleep(100000);
    return 0;
}
