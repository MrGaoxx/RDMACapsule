#include <poll.h>

#include "RDMAStack.h"
#include "msg/async/net_handler.h"

#undef dout_prefix
#define dout_prefix *_dout << " RDMAIWARPServerSocketImpl "

RDMAIWARPServerSocketImpl::RDMAIWARPServerSocketImpl(Context *context, std::shared_ptr<Infiniband> &ib,
                                                     std::shared_ptr<RDMADispatcher> &rdma_dispatcher, RDMAWorker *w, entity_addr_t &a,
                                                     unsigned addr_slot)
    : RDMAServerSocketImpl(context, ib, rdma_dispatcher, w, a, addr_slot) {}

int RDMAIWARPServerSocketImpl::listen(entity_addr_t &sa, const SocketOptions &opt) {
    std::cout << __func__ << " bind to rdma point" << std::endl;
    cm_channel = rdma_create_event_channel();
    rdma_create_id(cm_channel, &cm_id, NULL, RDMA_PS_TCP);
    std::cout << __func__ << " successfully created cm id: " << cm_id << std::endl;
    int rc = rdma_bind_addr(cm_id, const_cast<struct sockaddr *>(sa.get_sockaddr()));
    if (rc < 0) {
        rc = -errno;
        std::cout << __func__ << " unable to bind to " << sa.get_sockaddr() << " on port " << sa.get_port() << ": " << cpp_strerror(errno)
                  << std::endl;
        goto err;
    }
    rc = rdma_listen(cm_id, 128);
    if (rc < 0) {
        rc = -errno;
        std::cout << __func__ << " unable to listen to " << sa.get_sockaddr() << " on port " << sa.get_port() << ": " << cpp_strerror(errno)
                  << std::endl;
        goto err;
    }
    server_setup_socket = cm_channel->fd;
    rc = net.set_nonblock(server_setup_socket);
    if (rc < 0) {
        goto err;
    }
    std::cout << __func__ << " fd of cm_channel is " << server_setup_socket << std::endl;
    return 0;

err:
    server_setup_socket = -1;
    rdma_destroy_id(cm_id);
    rdma_destroy_event_channel(cm_channel);
    return rc;
}

int RDMAIWARPServerSocketImpl::accept(ConnectedSocket *sock, const SocketOptions &opt, entity_addr_t *out, Worker *w) {
    std::cout << __func__ << std::endl;

    kassert(sock);
    struct pollfd pfd = {
        .fd = cm_channel->fd,
        .events = POLLIN,
        .revents = 0,
    };
    int ret = poll(&pfd, 1, 0);
    kassert(ret >= 0);
    if (!ret) return -EAGAIN;

    struct rdma_cm_event *cm_event;
    rdma_get_cm_event(cm_channel, &cm_event);
    std::cout << __func__ << " event name: " << rdma_event_str(cm_event->event) << std::endl;

    struct rdma_cm_id *event_cm_id = cm_event->id;
    struct rdma_event_channel *event_channel = rdma_create_event_channel();

    if (net.set_nonblock(event_channel->fd) < 0) {
        std::cout << __func__ << " failed to switch event channel to non-block, close event channel " << std::endl;
        rdma_destroy_event_channel(event_channel);
        rdma_ack_cm_event(cm_event);
        return -errno;
    }

    rdma_migrate_id(event_cm_id, event_channel);

    struct rdma_conn_param *remote_conn_param = &cm_event->param.conn;
    struct rdma_conn_param local_conn_param;

    RDMACMInfo info(event_cm_id, event_channel, remote_conn_param->qp_num);
    RDMAIWARPConnectedSocketImpl *server = new RDMAIWARPConnectedSocketImpl(context, ib, dispatcher, dynamic_cast<RDMAWorker *>(w), &info);

    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&local_conn_param, 0, sizeof(local_conn_param));
    local_conn_param.qp_num = server->get_local_qpn();

    if (rdma_accept(event_cm_id, &local_conn_param)) {
        return -EAGAIN;
    }
    server->activate();
    std::cout << __func__ << " accepted a new QP" << std::endl;

    rdma_ack_cm_event(cm_event);

    std::unique_ptr<RDMAConnectedSocketImpl> csi(server);
    *sock = ConnectedSocket(std::move(csi));
    struct sockaddr *addr = &event_cm_id->route.addr.dst_addr;
    out->set_sockaddr(addr);

    return 0;
}

void RDMAIWARPServerSocketImpl::abort_accept() {
    if (server_setup_socket >= 0) {
        rdma_destroy_id(cm_id);
        rdma_destroy_event_channel(cm_channel);
    }
}
