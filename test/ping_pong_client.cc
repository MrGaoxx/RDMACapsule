#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "RDMAStack.h"
#include "common/context.h"
#include "core/Infiniband.h"
class RDMAClient {
   public:
    RDMAClient();
    int Init();

   private:
    Context cct;
};

int main(int argc, char** argv) {
    RDMAConfig* config = new RDMAConfig(std::string(argv[1]));
    config->m_op_threads_num_ = 1;
    Context* cct = new Context(config);
    std::shared_ptr<NetworkStack> networkStack = NetworkStack::create(cct, "rdma");

    networkStack->start();

    // for (int i = 0; i < networkStack->get_num_worker(); i++) {
    // use the first worker to connect
    Worker* worker = networkStack->get_worker(0);

    entity_addr_t server_addr(entity_addr_t::type_t::TYPE_SERVER, 0);  // nonce = 0
    server_addr.set_family(AF_INET);
    // sockaddr sa;
    sockaddr_in server_sock_addr;
    inet_pton(AF_INET, config->m_ipAddr.c_str(), &server_sock_addr.sin_addr);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(argv[3]));
    // sa.sin_zero = ;
    server_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&server_sock_addr));

    entity_addr_t client_addr(entity_addr_t::type_t::TYPE_SERVER, 0);  // nonce = 0
    client_addr.set_family(AF_INET);
    // sockaddr sa;
    sockaddr_in ca;
    inet_pton(AF_INET, config->m_ipAddr.c_str(), &sa.sin_addr);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(argv[3]));
    // sa.sin_zero = ;
    server_addr.set_sockaddr(reinterpret_cast<const sockaddr*>(&sa));

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

    ConnectedSocket cs;
    if (error = worker->connect(server_addr, opts, &cs)) {
        std::cout << "worker cannot connect addr" << server_addr << cpp_strerror(error) << std::endl;
        return;
    };
    reinterpret_cast<RDMAWorker*>(worker)->ib->;
    BufferList bl();
    Buffer buffer;

    buffer.buffer.bl.Append(buffer);
    cs.send(, );
}
