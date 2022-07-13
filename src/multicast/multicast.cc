#include "multicast/multicast.h"

MulticastDaemon::MulticastDaemon(Context *c) : Server(c) {
    // multicast_map[entity_addr_t("172.16.0.11", 30000)] = 1;
    multicast_addrs[mc_id] = {entity_addr_t("172.16.100.12", 30000), entity_addr_t("172.16.0.15", 30000)};

    mc_id++;

    mc_client_conn_read_callback = std::bind(&MulticastDaemon::process_client_read, this, nullptr);
    mc_server_conn_read_callback = std::bind(&MulticastDaemon::process_server_read, this, nullptr);
}
MulticastDaemon::~MulticastDaemon() {}

void MulticastDaemon::accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr) {
    std::cout << typeid(this).name() << __func__ << std::endl;

    std::lock_guard l{lock};
    auto accepting_conn = new Connection(context, this, w);
    accepting_conn->accept(std::move(cli_socket), listen_addr, peer_addr);
    accepting_conns.insert(accepting_conn);
    accepting_conn->read_callback = &mc_client_conn_read_callback;

    std::lock_guard data{data_lock};
    // kassert(multicast_connections.count(mc_id) == 0);
    accepting_conn->set_mc_id(mc_id);
    multicast_connections[mc_id] = std::array<Connection *, kNumMulticasts>();
    multicast_state[mc_id] = MCState();
    auto &mc_state = multicast_state[mc_id];
    // mc_state.client_state = MCState::ClientState::STATE_INIT;

    std::cout << "CREATING CONNECTIONS TO OTHER SERVERS... " << std::endl;
    for (int i = 0; i < kNumMulticasts; i++) {
        auto &addr = multicast_addrs[mc_id][i];
        std::cout << typeid(this).name() << " : " << __func__ << " " << addr << ", creating connection and registering" << std::endl;

        entity_addr_t target = addr;
        // create connection
        Worker *w = stack->get_worker();
        auto new_conn = new Connection(context, this, w);
        new_conn->read_callback = &mc_server_conn_read_callback;
        new_conn->connect(target);
        conns[target] = new_conn;
        new_conn->set_mc_id(mc_id);
        multicast_connections[mc_id][i] = new_conn;
        // mc_state.server_state[i] = MCState::ServerState::STATE_INIT;
    }
    // mc_state.client_state = MCState::ClientState::STATE_;
    mc_id++;
}

void MulticastDaemon::process_client_read(Connection *conn) {
    char msg[TCP_MSG_LEN];
    int read_size = 0;
    {
        std::lock_guard iol{io_lock};
        while (read_size < TCP_MSG_LEN) {
            int read_bytes = conn->Read(msg, TCP_MSG_LEN - read_size);
            if (unlikely(read_bytes < 0 && read_bytes != -EAGAIN)) {
                std::cout << typeid(this).name() << " : " << __func__ << " got error " << read_bytes << ": " << cpp_strerror(read_bytes) << std::endl;
            } else {  // tbd, disconnection message is of length 0
                read_size += read_bytes;
            }
        }
    }

    {
        std::lock_guard dl{data_lock};

        uint64_t mc_id = conn->get_mc_id();
        // multicast_map[conn->get_peer_socket_addr()];
        kassert(multicast_cm_meta.count(mc_id) == 0);
        auto &mc_state = multicast_state[mc_id];
        multicast_cm_meta[mc_id] = multicast_cm_meta_t();
        auto &mc_cm_meta = multicast_cm_meta[mc_id];
        char temp_gid[33];
        sscanf(msg, "%hx:%x:%x:%x:%s", &(mc_cm_meta.lid), &(mc_cm_meta.local_qpn), &(mc_cm_meta.sender_psn), &(mc_cm_meta.sender_qpn), temp_gid);
        multicast_cm_meta_t::wire_gid_to_gid(temp_gid, &mc_cm_meta);
        std::cout << __func__ << "RECEiVED CLIENT " << mc_id << " HANDSHAKE MSG" << mc_cm_meta << std::endl;

        switch (mc_state.client_state) {
            case MCState::ClientState::STATE_INIT: {
                kassert(mc_cm_meta.sender_psn == 0);
                kassert(mc_cm_meta.local_qpn == 0);
                mc_cm_meta.local_qpn = mc_id;

                mc_state.client_state = MCState::ClientState::STATE_HANDSHAKE_RECEIVED;
                // sending handshake to servers
                for (int i = 0; i < kNumMulticasts; i++) {
                    send_handshake_to_server(msg, mc_id, mc_cm_meta, mc_state, i);
                    mc_state.server_state[i] = MCState::ServerState::STATE_HANDSHAKE_SENT;
                }
                // sneding handshake to client
                send_handshake_to_client(conn, msg, mc_id, mc_cm_meta, mc_state);
                mc_state.client_state = MCState::ClientState::STATE_HANDSHAKE_SENT;
                break;
            }
            case MCState::ClientState::STATE_HANDSHAKE_SENT: {
                kassert(mc_cm_meta.sender_psn == 0);
                kassert(mc_cm_meta.local_qpn == mc_id);
                // send_handshake_to_client(conn, msg, mc_id, mc_cm_meta, mc_state);
                mc_state.client_state = MCState::ClientState::STATE_ACK_RECEIVED;
                break;
            }
            case MCState::ClientState::STATE_HANDSHAKE_RECEIVED:
            case MCState::ClientState::STATE_ACK_RECEIVED:
            default:
                std::cout << __func__ << "error state when process client read, STATE: " << mc_state.client_state << std::endl;
                assert(false);
        }
    }
}

void MulticastDaemon::process_server_read(Connection *conn) {
    char msg[TCP_MSG_LEN];
    int read_size = 0;
    {
        std::lock_guard iol{io_lock};
        while (read_size < TCP_MSG_LEN) {
            int read_bytes = conn->Read(msg, TCP_MSG_LEN - read_size);
            if (unlikely(read_bytes < 0 && read_bytes != -EAGAIN)) {
                std::cout << typeid(this).name() << " : " << __func__ << " got error " << read_bytes << ": " << cpp_strerror(read_bytes) << std::endl;
            } else {  // tbd, disconnection message is of length 0
                read_size += read_bytes;
            }
        }
    }

    {
        std::lock_guard dl{data_lock};
        uint64_t mc_id = conn->get_mc_id();
        kassert(multicast_cm_meta.count(mc_id) == 0);
        auto &mc_state = multicast_state[mc_id];
        int index = get_mc_index(mc_id, conn);
        multicast_cm_meta[mc_id] = multicast_cm_meta_t();
        auto &mc_cm_meta = multicast_cm_meta[mc_id];
        char temp_gid[33];
        uint32_t receiver_lid, receiver_qpn;
        sscanf(msg, "%hx:%x:%x:%x:%s", &receiver_lid, &receiver_qpn, &(mc_cm_meta.receiver_psn[index]), &(mc_cm_meta.receiver_psn[index]), temp_gid);
        multicast_cm_meta_t::wire_gid_to_gid(temp_gid, &mc_cm_meta);
        std::cout << __func__ << "RECEIVED SERVER " << mc_id << ":" << index << " HANDSHAKE MSG" << mc_cm_meta << std::endl;

        switch (mc_state.server_state[index]) {
            case MCState::ServerState::STATE_INIT:
                // the server has sent msg befor client, waiting for the client handshake msg
                break;
            case MCState::ServerState::STATE_HANDSHAKE_SENT: {
                // sending handshake ACK to this servers
                send_handshake_to_server(msg, mc_id, mc_cm_meta, mc_state, index);
                mc_state.server_state[index] = MCState::ServerState::STATE_HANDSHAKE_ACK_SENT;
                break;
            }
            default:
                std::cout << __func__ << "error state when process client read, STATE: " << mc_state.client_state << std::endl;
                assert(false);
        }
    }
}

void MulticastDaemon::send_handshake_to_client(Connection *conn, char *msg, mc_id_t mc_id, multicast_cm_meta_t &mc_cm_meta, MCState &mc_state) {
    // std::cout << __func__ << std::endl;
    int retry = 0;
    char temp_gid[33];
    multicast_cm_meta_t::gid_to_wire_gid(mc_cm_meta, temp_gid);
    sprintf(msg, "%04x:%08x:%08x:%08x:%s", mc_cm_meta.lid, mc_id, mc_cm_meta.sender_psn, mc_cm_meta.sender_qpn, temp_gid);

    std::cout << "Sending handshake msgs to mc_id:" << mc_id << " client" << std::endl;
    std::cout << typeid(this).name() << " : " << __func__ << " sending: " << mc_cm_meta.lid << ", " << mc_cm_meta.sender_qpn << ", "
              << mc_cm_meta.sender_psn << ", " << mc_cm_meta.receiver_qpn[0] << "," << mc_cm_meta.receiver_psn[0] << ", " << temp_gid << std::endl;

    std::lock_guard iol{conn->get_write_lock()};
retry:
    auto r = conn->write(msg, sizeof(msg));
    if (unlikely((size_t)r != sizeof(msg))) {
        // FIXME need to handle EAGAIN instead of retry
        if (r < 0 && (errno == EINTR || errno == EAGAIN) && retry < 3) {
            retry++;
            goto retry;
        }
        if (r < 0)
            std::cout << typeid(this).name() << " : " << __func__ << " send returned error " << errno << ": " << cpp_strerror(errno) << std::endl;
        else
            std::cout << typeid(this).name() << " : " << __func__ << " send got bad length (" << r << ") " << cpp_strerror(errno) << std::endl;
        mc_state.client_state = MCState::ClientState::STATE_ERROR;
        assert(false);
    }
};

void MulticastDaemon::send_handshake_to_server(char *msg, mc_id_t mc_id, multicast_cm_meta_t &mc_cm_meta, MCState &mc_state, int i) {
    std::cout << __func__ << std::endl;
    Connection *conn = multicast_connections[mc_id][i];
    int retry = 0;
    char temp_gid[33];
    kassert(mc_state.server_state[i] == MCState::ServerState::STATE_INIT);
    multicast_cm_meta_t::gid_to_wire_gid(mc_cm_meta, temp_gid);
    sprintf(msg, "%04x:%08x:%08x:%08x:%s", mc_cm_meta.lid, mc_cm_meta.sender_qpn, mc_cm_meta.sender_psn, mc_cm_meta.receiver_qpn[i], temp_gid);

    std::cout << "Sending handshake msgs to mc_id:" << mc_id << " No." << i << " server" << std::endl;
    std::cout << typeid(this).name() << " : " << __func__ << " sending: " << mc_cm_meta.lid << ", " << mc_cm_meta.sender_qpn << ", "
              << mc_cm_meta.sender_psn << ", " << mc_cm_meta.receiver_qpn[i] << "," << mc_cm_meta.receiver_psn[i] << ", " << temp_gid << std::endl;

    std::lock_guard iol{conn->get_write_lock()};
retry:
    auto r = conn->write(msg, sizeof(msg));
    if (unlikely((size_t)r != sizeof(msg))) {
        // FIXME need to handle EAGAIN instead of retry
        if (r < 0 && (errno == EINTR || errno == EAGAIN) && retry < 3) {
            retry++;
            goto retry;
        }
        if (r < 0)
            std::cout << typeid(this).name() << " : " << __func__ << " send returned error " << errno << ": " << cpp_strerror(errno) << std::endl;
        else
            std::cout << typeid(this).name() << " : " << __func__ << " send got bad length (" << r << ") " << cpp_strerror(errno) << std::endl;
        mc_state.server_state[i] = MCState::ServerState::STATE_ERROR;
        assert(false);
    }
}
