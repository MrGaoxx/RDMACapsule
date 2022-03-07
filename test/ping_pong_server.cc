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
    Context* cct = new Context(config);
    std::shared_ptr<NetworkStack> networkStack = NetworkStack::create(cct, "rdma");

    networkStack->start();
    for (int i = 0; i < networkStack->get_num_worker(); i++) {
        Worker* worker = networkStack->get_worker(i);
        worker->listen();
        worker->
    }
}
