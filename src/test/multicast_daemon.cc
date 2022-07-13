
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <cerrno>
#include <functional>

#include "RDMAStack.h"
#include "common.h"
#include "common/context.h"
#include "core/Infiniband.h"
#include "multicast/multicast.h"

class MulticastDaemonApp {
   public:
    MulticastDaemonApp(std::string& configFileName);
    ~MulticastDaemonApp();

    void Init();
    int Listen();
    void Poll();

    void ProcessRequest(Connection*);

   private:
    bool listening;
    Config* config;
    Context* context;

    MulticastDaemon mc_daemon;

    entity_addr_t server_addr;
    entity_addr_t client_addr;
};

MulticastDaemonApp::MulticastDaemonApp(std::string& configFileName)
    : listening(false),
      config(new Config(configFileName)),
      context(new Context(config)),
      mc_daemon(context),
      server_addr(entity_addr_t::type_t::TYPE_SERVER, 0),
      client_addr(entity_addr_t::type_t::TYPE_CLIENT, 0) {}
MulticastDaemonApp::~MulticastDaemonApp() {
    delete config;
    delete context;
}
void MulticastDaemonApp::Init() { mc_daemon.start(); }
int MulticastDaemonApp::Listen() {
    if (unlikely(listening)) {
        return -EBUSY;
    }

    server_addr.set_addr(config->m_ip_addr.c_str(), config->m_listen_port);
    std::cout << "MCDAEMON:: listening on the addr" << server_addr << std::endl;
    return mc_daemon.bind(server_addr);
}

int main(int argc, char* argv[]) {
    std::cout << "The filename of configuration file is: " << std::string(argv[1]) << std::endl;
    std::string configFileName(argv[1]);
    MulticastDaemonApp server(configFileName);
    server.Init();
    int error = server.Listen();
    if (unlikely(error)) {
        std::cout << "worker cannot listen socket on addr" << cpp_strerror(error) << std::endl;
        return 1;
    } else {
        std::cout << "==========> listening socket succeeded" << std::endl;
    };
    sleep(100000);
    return 0;
}
