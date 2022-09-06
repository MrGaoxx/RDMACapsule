#ifndef MULTICAST_H
#define MULTICAST_H
#include <array>

#include "common/types.h"
#include "controller/mc_control_plane.h"
#include "core/server.h"

static const int kNumMulticasts = 2;
using mc_id_t = uint32_t;
struct multicast_cm_meta_t {
    static void wire_gid_to_gid(const char *wgid, ibv_gid *gid) {
        char tmp[9];
        uint32_t v32;
        int i;

        for (tmp[8] = 0, i = 0; i < 4; ++i) {
            memcpy(tmp, wgid + i * 8, 8);
            sscanf(tmp, "%x", &v32);
            *(uint32_t *)(&(gid->raw[i * 4])) = ntohl(v32);
        }
    }

    static void gid_to_wire_gid(ibv_gid *gid, char wgid[]) {
        for (int i = 0; i < 4; ++i) sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(gid->raw + i * 4)));
    }

    // sender
    uint16_t sender_lid;
    uint32_t sender_local_qpn;
    uint32_t sender_psn;
    uint32_t sender_peer_qpn;

    std::array<uint16_t, kNumMulticasts> receiver_lid;
    std::array<uint32_t, kNumMulticasts> receiver_local_qpn;
    std::array<uint32_t, kNumMulticasts> receiver_psn;
    std::array<uint32_t, kNumMulticasts> receiver_peer_qpn;

    union ibv_gid sender_gid;
    std::array<ibv_gid, kNumMulticasts> receiver_gid;
} __attribute__((packed));

inline std::ostream &operator<<(std::ostream &os, const multicast_cm_meta_t &mc_meta) {
    os << "mc_meta.sender_lid " << mc_meta.sender_lid << "\n"
       << "mc_meta.sender_local_qpn " << mc_meta.sender_local_qpn << "\n"
       << "mc_meta.sender_psn " << mc_meta.sender_psn << "\n"
       << "mc_meta.sender_peer_qpn " << mc_meta.sender_peer_qpn << "\n";
    //<< "mc_meta.gid" << mc_meta.gid << "\n"
    //<< "mc_meta.local_qpn" << mc_meta.local_qpn << "\n";
    os << "mc_meta.receiver_qpn ";
    for (int i = 0; i < kNumMulticasts; i++) {
        os << "\t" << mc_meta.receiver_lid[i];
    }
    os << "\n";

    os << "mc_meta.receiver_local_qpn ";
    for (int i = 0; i < kNumMulticasts; i++) {
        os << "\t" << mc_meta.receiver_local_qpn[i];
    }
    os << "\n";

    os << "mc_meta.receiver_psn ";
    for (int i = 0; i < kNumMulticasts; i++) {
        os << "\t" << mc_meta.receiver_psn[i];
    }
    os << "\n";

    os << "mc_meta.receiver_peer_qpn ";
    for (int i = 0; i < kNumMulticasts; i++) {
        os << "\t" << mc_meta.receiver_peer_qpn[i];
    }
    os << "\n";

    os << "mc_meta.sender_gid ";
    for (int i = 0; i < 16; i++) {
        os << mc_meta.sender_gid.raw[i];
    }

    os << "mc_meta.receiver_gid: \n";
    for (int i = 0; i < kNumMulticasts; i++) {
        os << i << " gid: ";
        for (int j = 0; j < 16; j++) {
            os << mc_meta.receiver_gid[i].raw[j];
        }
        os << "\n";
    }
    os << "\n";
    os << std::endl;
    return os;
}

struct MCState {
    enum class ClientState { STATE_INIT = 0, STATE_HANDSHAKE_RECEIVED, STATE_HANDSHAKE_SENT, STATE_ACK_RECEIVED, STATE_ERROR };
    enum class ServerState {
        STATE_INIT = static_cast<int>(ClientState::STATE_ERROR) + 1,
        STATE_CONNECTED,
        STATE_HANDSHAKE_SENT,
        STATE_HANDSHAKE_ACK_SENT,
        STATE_ERROR
    };
    MCState() : client_state(ClientState::STATE_INIT) {
        for (int i = 0; i < kNumMulticasts; i++) {
            server_state[i] = ServerState::STATE_INIT;
        }
    };
    ClientState client_state;
    std::array<ServerState, kNumMulticasts> server_state;
};

inline std::ostream &operator<<(std::ostream &os, MCState::ClientState cstate) {
    switch (cstate) {
        case MCState::ClientState::STATE_INIT:
            os << "STATE_INIT" << std::endl;
            break;
        case MCState::ClientState::STATE_HANDSHAKE_RECEIVED:
            os << "STATE_HANDSHAKE_RECEIVED" << std::endl;
            break;
        case MCState::ClientState::STATE_HANDSHAKE_SENT:
            os << "STATE_HANDSHAKE_SENT" << std::endl;
            break;
        case MCState::ClientState::STATE_ACK_RECEIVED:
            os << "STATE_ACK_RECEIVED" << std::endl;
            break;
        case MCState::ClientState::STATE_ERROR:
            os << "STATE_ERROR" << std::endl;
            break;
        default:
            os << "UNKNOWN_STATE" << std::endl;
    }
    return os;
}

inline std::ostream &operator<<(std::ostream &os, MCState::ServerState cstate) {
    switch (cstate) {
        case MCState::ServerState::STATE_INIT:
            os << "STATE_INIT" << std::endl;
            break;
        case MCState::ServerState::STATE_CONNECTED:
            os << "STATE_CONNECTED" << std::endl;
            break;
        case MCState::ServerState::STATE_HANDSHAKE_SENT:
            os << "STATE_HANDSHAKE_SENT" << std::endl;
            break;
        case MCState::ServerState::STATE_HANDSHAKE_ACK_SENT:
            os << "STATE_HANDSHAKE_ACK_SENT" << std::endl;
            break;
        case MCState::ServerState::STATE_ERROR:
            os << "STATE_ERROR" << std::endl;
            break;
        default:
            os << "UNKNOWN_STATE" << std::endl;
    }
    return os;
}

class MulticastDaemon : public Server {
   public:
    MulticastDaemon(Context *context);
    ~MulticastDaemon();

    // int bind(const entity_addr_t &bind_addr);
    void accept(Worker *w, ConnectedSocket cli_socket, const entity_addr_t &listen_addr, const entity_addr_t &peer_addr) override;

    std::function<void(Connection *)> mc_client_conn_read_callback;
    std::function<void(Connection *)> mc_client_conn_write_callback;
    std::function<void(Connection *)> mc_server_conn_read_callback;
    std::function<void(Connection *)> mc_server_conn_write_callback;

   private:
    std::mutex data_lock;
    // std::unordered_map<entity_addr_t, mc_id_t> multicast_map;
    std::unordered_map<mc_id_t, multicast_cm_meta_t> multicast_cm_meta;
    std::unordered_map<mc_id_t, MCState> multicast_state;
    std::unordered_map<mc_id_t, std::array<Connection *, kNumMulticasts + 1>> multicast_connections;
    std::unordered_map<mc_id_t, std::array<entity_addr_t, kNumMulticasts>> multicast_addrs;
    SwitchTableWritter p4_writter;

    std::mutex io_lock;
    void process_client_readable(Connection *);
    void process_client_writeable(Connection *);
    void process_server_readable(Connection *);
    void process_server_writeable(Connection *);

    void check_and_send_handshake_to_client(Connection *, mc_id_t, multicast_cm_meta_t &, MCState &);
    void send_handshake_to_server(mc_id_t, multicast_cm_meta_t &, MCState &, int i);
    int get_mc_index(mc_id_t mc_id, Connection *conn) {
        kassert(multicast_connections.count(mc_id) == 1);
        int i = 0;
        for (; i < kNumMulticasts; i++) {
            if (multicast_connections[mc_id][i + 1] == conn) break;
        }
        return i;
    }
    mc_id_t m_mc_id;
};
;

#endif