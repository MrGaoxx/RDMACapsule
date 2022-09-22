#include "multicast/multicast.h"

//
MulticastDaemon::MulticastDaemon(Context *c) : Server(c), m_mc_id((((c->m_rdma_config_->m_addr.in4_addr().sin_addr.s_addr) << 12) + rand()) % 10000) {
    // multicast_map[entity_addr_t("172.16.0.11", 30000)] = 1;
    // multicast_addrs[mc_id] = {entity_addr_t("172.16.0.15", 30000), entity_addr_t("172.16.0.16", 30000)};
    // mc_id++;
    mc_client_conn_read_callback = std::bind(&MulticastDaemon::process_client_readable, this, std::placeholders::_1);
    mc_client_conn_write_callback = std::bind(&MulticastDaemon::process_client_writeable, this, std::placeholders::_1);
    mc_server_conn_read_callback = std::bind(&MulticastDaemon::process_server_readable, this, std::placeholders::_1);
    mc_server_conn_write_callback = std::bind(&MulticastDaemon::process_server_writeable, this, std::placeholders::_1);
    srand(Cycles::get_soft_timestamp_us());
    m_mc_id = (((c->m_rdma_config_->m_addr.in4_addr().sin_addr.s_addr) << 12) + rand()) % 10000;
    {
        std::cout << "Switch address is " << p4_writter.get_switch_addr() << std::endl;
        // p4_writter.init_switch_table();
    }
}
MulticastDaemon::~MulticastDaemon() {}

void MulticastDaemon::accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr) {
    std::cout << typeid(this).name() << __func__ << std::endl;

    std::lock_guard l{lock};
    auto accepting_conn = new Connection(context, this, w);
    accepting_conn->accept(std::move(cli_socket), listen_addr, peer_addr);
    accepting_conns.insert(accepting_conn);
    accepting_conn->read_callback = &mc_client_conn_read_callback;
    accepting_conn->write_callback = &mc_client_conn_write_callback;
}

void MulticastDaemon::process_client_readable(Connection *conn) {
    std::cout << __func__ << std::endl;
    char msg[TCP_MSG_LEN];
    uint32_t read_size = 0;
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

    if (conn->get_mc_id() == 0) {  // not aligned mc_id

        std::cout << "CREATING CONNECTIONS TO OTHER SERVERS... " << std::endl;
        std::lock_guard data{data_lock};
        mc_id_t mc_id = m_mc_id++;
        conn->set_mc_id(mc_id);
        std::cout << "MCID " << mc_id << std::endl;
        multicast_connections[mc_id] = std::array<Connection *, kNumMulticasts + 1>();
        multicast_connections[mc_id][0] = conn;
        multicast_state[mc_id] = MCState();
        kassert(multicast_cm_meta.count(mc_id) == 0);
        multicast_cm_meta[mc_id] = multicast_cm_meta_t();
        auto &mc_cm_meta = multicast_cm_meta[mc_id];
        char temp_gid[33];
        multicast_addrs[mc_id] = {};
        std::cout << "MCID " << mc_id << std::endl;
        uint32_t ip_addr_member_1;
        uint16_t port_member_1;
        uint32_t ip_addr_member_2;
        uint16_t port_member_2;
        sscanf(msg, "%hx:%x:%x:%x:%x:%hx:%x:%hx:%s", &(mc_cm_meta.sender_lid), &(mc_cm_meta.sender_local_qpn), &(mc_cm_meta.sender_psn),
               &(mc_cm_meta.sender_peer_qpn), &ip_addr_member_1, &port_member_1, &ip_addr_member_2, &port_member_2, temp_gid);
        in_addr addr;
        std::cout << "MCID " << mc_id << std::endl;
        addr.s_addr = ip_addr_member_1;
        strcpy(multicast_addrs[mc_id][0].ip_char, inet_ntoa(addr));
        multicast_addrs[mc_id][0].set_addr(multicast_addrs[mc_id][0].ip_char, port_member_1);
        addr.s_addr = ip_addr_member_2;
        strcpy(multicast_addrs[mc_id][1].ip_char, inet_ntoa(addr));
        multicast_addrs[mc_id][1].set_addr(multicast_addrs[mc_id][1].ip_char, port_member_2);
        multicast_cm_meta_t::wire_gid_to_gid(temp_gid, &mc_cm_meta.sender_gid);
        std::cout << "MCID " << mc_id << std::endl;
        std::cout << __func__ << ": RECEIVED CLIENT " << static_cast<int>(mc_id) << " MULTCAST ADDR " << multicast_addrs[mc_id][0] << " "
                  << multicast_addrs[mc_id][1] << " OTHER HANDSHAKE MSG " << mc_cm_meta << std::endl;

        auto &mc_state = multicast_state[mc_id];
        mc_state.client_state = MCState::ClientState::STATE_HANDSHAKE_RECEIVED;

        for (int i = 0; i < kNumMulticasts; i++) {
            auto &addr = multicast_addrs[mc_id][i];
            std::cout << typeid(this).name() << " : " << __func__ << " creating connection and registering to " << addr << std::endl;

            entity_addr_t target = addr;
            // create connection
            Worker *w = stack->get_worker();
            auto new_conn = new Connection(context, this, w);
            new_conn->read_callback = &mc_server_conn_read_callback;
            new_conn->write_callback = &mc_server_conn_write_callback;
            new_conn->connect(target);
            conns[target] = new_conn;
            new_conn->set_mc_id(mc_id);
            multicast_connections[mc_id][i + 1] = new_conn;
        }

    } else {
        auto mc_id = conn->get_mc_id();
        auto &mc_state = multicast_state[mc_id];
        auto &mc_cm_meta = multicast_cm_meta[mc_id];
        char temp_gid[33];
        uint32_t unused32;
        uint16_t unused16;
        sscanf(msg, "%hx:%x:%x:%x:%x:%hx:%x:%hx:%s", &(mc_cm_meta.sender_lid), &(mc_cm_meta.sender_local_qpn), &(mc_cm_meta.sender_psn),
               &(mc_cm_meta.sender_peer_qpn), &unused32, &unused16, &unused32, &unused16, temp_gid);
        multicast_cm_meta_t::wire_gid_to_gid(temp_gid, &mc_cm_meta.sender_gid);
        std::cout << __func__ << ": RECEIVED CLIENT " << mc_id << " HANDSHAKE MSG " << mc_cm_meta << std::endl;
        kassert(mc_state.client_state == MCState::ClientState::STATE_HANDSHAKE_SENT);
        switch (mc_state.client_state) {
            case MCState::ClientState::STATE_HANDSHAKE_SENT: {
                kassert(mc_cm_meta.sender_peer_qpn == mc_id);
                // send_handshake_to_client(conn, msg, mc_id, mc_cm_meta, mc_state);
                mc_state.client_state = MCState::ClientState::STATE_ACK_RECEIVED;
                break;
            }
            case MCState::ClientState::STATE_INIT:
            case MCState::ClientState::STATE_HANDSHAKE_RECEIVED:
            case MCState::ClientState::STATE_ACK_RECEIVED:
            default:
                std::cout << __func__ << " error state when process client read, STATE: " << mc_state.client_state << std::endl;
                assert(false);
        }

        /*
        switch (mc_state.client_state) {
            case MCState::ClientState::STATE_INIT: {
                kassert(mc_cm_meta.sender_peer_qpn == 0);
                mc_cm_meta.sender_peer_qpn = mc_id;

                mc_state.client_state = MCState::ClientState::STATE_HANDSHAKE_RECEIVED;
                // sending handshake to servers
                for (int i = 0; i < kNumMulticasts; i++) {
                    if (mc_state.server_state[i] == MCState::ServerState::STATE_CONNECTED) {
                        send_handshake_to_server(mc_id, mc_cm_meta, mc_state, i);
                        mc_state.server_state[i] = MCState::ServerState::STATE_HANDSHAKE_SENT;
                    }
                }
                break;
            }
            case MCState::ClientState::STATE_HANDSHAKE_SENT: {
                kassert(mc_cm_meta.sender_peer_qpn == mc_id);
                // send_handshake_to_client(conn, msg, mc_id, mc_cm_meta, mc_state);
                mc_state.client_state = MCState::ClientState::STATE_ACK_RECEIVED;
                break;
            }
            case MCState::ClientState::STATE_HANDSHAKE_RECEIVED:
            case MCState::ClientState::STATE_ACK_RECEIVED:
            default:
                std::cout << __func__ << " error state when process client read, STATE: " << mc_state.client_state << std::endl;
                assert(false);
        }
        */
    }
}

void MulticastDaemon::process_server_readable(Connection *conn) {
    std::cout << __func__ << std::endl;
    char msg[TCP_MSG_LEN];
    uint32_t read_size = 0;
    {
        std::lock_guard iol{io_lock};
        while (read_size < TCP_MSG_LEN) {
            ssize_t read_bytes = conn->Read(msg, TCP_MSG_LEN - read_size);
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
        auto &mc_state = multicast_state[mc_id];
        int index = get_mc_index(mc_id, conn);
        auto &mc_cm_meta = multicast_cm_meta[mc_id];
        char temp_gid[33];
        uint32_t unused32;
        uint16_t unused16;
        sscanf(msg, "%hx:%x:%x:%x:%x:%hx:%x:%hx:%s", &mc_cm_meta.receiver_lid[index], &mc_cm_meta.receiver_local_qpn[index],
               &(mc_cm_meta.receiver_psn[index]), &(mc_cm_meta.receiver_peer_qpn[index]), &unused32, &unused16, &unused32, &unused16, temp_gid);
        multicast_cm_meta_t::wire_gid_to_gid(temp_gid, &mc_cm_meta.receiver_gid[index]);
        std::cout << __func__ << " RECEIVED SERVER " << mc_id << ":" << index << " HANDSHAKE MSG" << mc_cm_meta << std::endl;

        switch (mc_state.server_state[index]) {
            case MCState::ServerState::STATE_INIT:
                // the server has sent msg before client, waiting for the client handshake msg
                break;
            case MCState::ServerState::STATE_HANDSHAKE_SENT: {
                // sending handshake ACK to this servers
                send_handshake_to_server(mc_id, mc_cm_meta, mc_state, index);
                mc_state.server_state[index] = MCState::ServerState::STATE_HANDSHAKE_ACK_SENT;
                check_and_send_handshake_to_client(multicast_connections[mc_id][0], mc_id, mc_cm_meta, mc_state);
                break;
            }
            default:
                std::cout << __func__ << "error state when process server read, STATE: " << mc_state.server_state[index] << std::endl;
                assert(false);
        }
    }
}

void MulticastDaemon::check_and_send_handshake_to_client(Connection *conn, mc_id_t mc_id, multicast_cm_meta_t &mc_cm_meta, MCState &mc_state) {
    std::cout << __func__ << std::endl;
    switch (mc_state.client_state) {
        case MCState::ClientState::STATE_HANDSHAKE_RECEIVED:
            break;
        case MCState::ClientState::STATE_HANDSHAKE_SENT:
            std::cout << __func__ << " handshake already sent" << std::endl;
            return;
        default:
            std::cout << __func__ << " failed, client state: " << mc_state.client_state << std::endl;
            return;
    }

    // int ready_to_send = false;
    for (int i = 0; i < kNumMulticasts; i++) {
        if (mc_state.server_state[i] != MCState::ServerState::STATE_HANDSHAKE_ACK_SENT) {
            std::cout << __func__ << " failed, server state: " << mc_state.server_state[i] << std::endl;
            return;
        }
    }

    // p4_writter.multicast_group_del(mc_id, conn->get_local_addr().in4_addr().sin_addr.s_addr, mc_id, htonl(mc_cm_meta.sender_local_qpn),
    //                               multicast_addrs[mc_id][0].in4_addr().sin_addr.s_addr, multicast_addrs[mc_id][1].in4_addr().sin_addr.s_addr);
    std::cout << "Writing to table: " << std::hex << htonl(conn->get_peer_socket_addr().in4_addr().sin_addr.s_addr) << " " << mc_id << " "
              << mc_cm_meta.sender_local_qpn << " " << htonl(multicast_addrs[mc_id][0].in4_addr().sin_addr.s_addr) << " "
              << mc_cm_meta.receiver_local_qpn[0] << " " << htonl(multicast_addrs[mc_id][1].in4_addr().sin_addr.s_addr) << " "
              << mc_cm_meta.receiver_local_qpn[1] << std::endl;

    p4_writter.multicast_group_add(htonl(conn->get_peer_socket_addr().in4_addr().sin_addr.s_addr), mc_id, mc_cm_meta.sender_local_qpn,
                                   htonl(multicast_addrs[mc_id][0].in4_addr().sin_addr.s_addr), mc_cm_meta.receiver_local_qpn[0],
                                   htonl(multicast_addrs[mc_id][1].in4_addr().sin_addr.s_addr), mc_cm_meta.receiver_local_qpn[1]);

    int retry = 0;
    char temp_gid[33];
    char msg[TCP_MSG_LEN];
    multicast_cm_meta_t::gid_to_wire_gid(&mc_cm_meta.receiver_gid[1], temp_gid);
    sprintf(msg, "%04x:%08x:%08x:%08x:%08x:%04x:%08x:%04x:%s", mc_cm_meta.receiver_lid[0], mc_id, mc_cm_meta.sender_psn, mc_cm_meta.sender_local_qpn,
            mc_cm_meta.sender_psn, mc_cm_meta.receiver_lid[0], mc_cm_meta.sender_psn, mc_cm_meta.receiver_lid[0], temp_gid);

    std::cout << "Sending handshake msgs to mc_id:" << mc_id << " client" << std::endl;
    std::cout << typeid(this).name() << " : " << __func__ << " sending: " << mc_cm_meta.receiver_lid[0] << ", " << mc_id << ", "
              << mc_cm_meta.sender_psn << ", " << mc_cm_meta.sender_local_qpn << "," << temp_gid << std::endl;

    {
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
    }
    mc_state.client_state = MCState::ClientState::STATE_HANDSHAKE_SENT;
};

void MulticastDaemon::send_handshake_to_server(mc_id_t mc_id, multicast_cm_meta_t &mc_cm_meta, MCState &mc_state, int i) {
    std::cout << __func__ << std::endl;
    Connection *conn = multicast_connections[mc_id][i + 1];
    int retry = 0;
    char temp_gid[33];
    char msg[TCP_MSG_LEN];
    kassert(mc_state.server_state[i] == MCState::ServerState::STATE_CONNECTED ||
            mc_state.server_state[i] == MCState::ServerState::STATE_HANDSHAKE_SENT);
    multicast_cm_meta_t::gid_to_wire_gid(&mc_cm_meta.sender_gid, temp_gid);
    sprintf(msg, "%04x:%08x:%08x:%08x:%08x:%04x:%08x:%04x:%s", mc_cm_meta.sender_lid, mc_cm_meta.sender_local_qpn, mc_cm_meta.sender_psn,
            mc_cm_meta.receiver_local_qpn[i], mc_cm_meta.sender_local_qpn, mc_cm_meta.sender_lid, mc_cm_meta.sender_local_qpn, mc_cm_meta.sender_lid,
            temp_gid);

    std::cout << "Sending handshake msgs to mc_id:" << mc_id << " No." << i << " server" << std::endl;
    std::cout << typeid(this).name() << " : " << __func__ << " sending: " << mc_cm_meta.sender_lid << ", " << mc_cm_meta.sender_local_qpn << ", "
              << mc_cm_meta.sender_psn << ", " << mc_cm_meta.receiver_local_qpn[i] << ", " << temp_gid << std::endl;

    {
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
}

void MulticastDaemon::process_client_writeable(Connection *conn) { std::cout << __func__ << std::endl; }

void MulticastDaemon::process_server_writeable(Connection *conn) {
    std::cout << __func__ << std::endl;
    uint64_t mc_id = conn->get_mc_id();
    auto &mc_state = multicast_state[mc_id];
    int index = get_mc_index(mc_id, conn);
    auto &mc_cm_meta = multicast_cm_meta[mc_id];
    std::lock_guard dl{data_lock};
    mc_state.server_state[index] = MCState::ServerState::STATE_CONNECTED;
    if (mc_state.client_state == MCState::ClientState::STATE_HANDSHAKE_RECEIVED) {  // client msg befor connection established, sent here
        send_handshake_to_server(mc_id, mc_cm_meta, mc_state, index);
        mc_state.server_state[index] = MCState::ServerState::STATE_HANDSHAKE_SENT;
    } else {
        std::cout << mc_id << " conn " << index << " waiting client msg to send" << std::endl;
    }
}
