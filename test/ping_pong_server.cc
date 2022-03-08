#include <netinet/ip.h>

#include "RDMAStack.h"
#include "common/context.h"
#include "core/Infiniband.h"
class RDMAServer {
   public:
    RDMAServer();
    int Init();

   private:
    Context cct;
};

int main() {
    RDMAConfig* config = new RDMAConfig(std::string("mlx5_0"), 3, 0);
    config->m_op_threads_num_ = 1;
    Context* cct = new Context(config);
    std::shared_ptr<NetworkStack> networkStack = NetworkStack::create(cct, "rdma");

    networkStack->start();

    // for (int i = 0; i < networkStack->get_num_worker(); i++) {
    // use the first worker to connect
    Worker* worker = networkStack->get_worker(0);

    entity_addr_t server_addr(entity_addr_t::type_t::TYPE_SERVER, 0);  // nonce = 0

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
    if (error = worker->listen(server_addr, opts, sock)) {
        std::cout << "worker cannot listen socket on addr" << cpp_strerror(error) << std::endl;
        return;
    };

    ServerSocketImpl* sock_imp;
    sock_imp->accept();
}
