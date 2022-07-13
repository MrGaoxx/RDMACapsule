#ifndef MULTICAST_H
#define MULTICAST_H
#include <array>

#include "common/types.h"
#include "core/server.h"

static const int kNumMulticasts = 3;
using mc_id_t = uint32_t;
struct multicast_cm_meta_t {
    static void wire_gid_to_gid(const char *wgid, multicast_cm_meta_t *cm_meta_data) {
        char tmp[9];
        uint32_t v32;
        int i;

        for (tmp[8] = 0, i = 0; i < 4; ++i) {
            memcpy(tmp, wgid + i * 8, 8);
            sscanf(tmp, "%x", &v32);
            *(uint32_t *)(&cm_meta_data->gid.raw[i * 4]) = ntohl(v32);
        }
    }

    static void gid_to_wire_gid(const multicast_cm_meta_t &cm_meta_data, char wgid[]) {
        for (int i = 0; i < 4; ++i) sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(cm_meta_data.gid.raw + i * 4)));
    }
    uint16_t lid;

    uint32_t local_qpn;

    uint32_t sender_psn;
    std::array<uint32_t, kNumMulticasts> receiver_psn;

    uint32_t sender_qpn;
    uint32_t receiver_qpn[kNumMulticasts];

    union ibv_gid gid;
} __attribute__((packed));

inline std::ostream &operator<<(std::ostream &os, const multicast_cm_meta_t &mc_meta) {
    os << "mc_meta.lid" << mc_meta.lid << "\n"
       << "mc_meta.local_qpn" << mc_meta.local_qpn << "\n"
       << "mc_meta.sender_psn" << mc_meta.sender_psn << "\n"
       << "mc_meta.sender_qpn" << mc_meta.sender_qpn << "\n";
    //<< "mc_meta.gid" << mc_meta.gid << "\n"
    //<< "mc_meta.local_qpn" << mc_meta.local_qpn << "\n";
    os << " mc_meta.receiver_qpn";
    for (int i = 0; i < kNumMulticasts; i++) {
        os << "\t" << mc_meta.receiver_qpn[i];
    }
    os << "\n";

    os << " mc_meta.receiver_psn";
    for (int i = 0; i < kNumMulticasts; i++) {
        os << "\t" << mc_meta.receiver_psn[i];
    }
    os << "\n";

    os << "mc_meta.gid ";
    for (int i = 0; i < 16; i++) {
        os << "\t" << mc_meta.gid.raw[i] << " ";
    }
    os << std::endl;
}

struct MCState {
    enum class ClientState { STATE_INIT = 0, STATE_HANDSHAKE_RECEIVED, STATE_HANDSHAKE_SENT, STATE_ACK_RECEIVED, STATE_ERROR };
    enum class ServerState {
        STATE_INIT = static_cast<int>(ClientState::STATE_ERROR) + 1,
        STATE_HANDSHAKE_SENT,
        STATE_HANDSHAKE_ACK_SENT,
        STATE_ERROR
    };
    MCState() : client_state(ClientState::STATE_INIT), server_state(){};
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
    std::function<void(Connection *)> mc_server_conn_read_callback;

   private:
    std::mutex data_lock;
    // std::unordered_map<entity_addr_t, mc_id_t> multicast_map;
    std::unordered_map<mc_id_t, multicast_cm_meta_t> multicast_cm_meta;
    std::unordered_map<mc_id_t, MCState> multicast_state;
    std::unordered_map<mc_id_t, std::array<Connection *, kNumMulticasts>> multicast_connections;
    std::unordered_map<mc_id_t, std::array<entity_addr_t, kNumMulticasts>> multicast_addrs;

    std::mutex io_lock;
    void process_client_read(Connection *);
    void process_server_read(Connection *);

    void send_handshake_to_client(Connection *, char *, mc_id_t, multicast_cm_meta_t &, MCState &);
    void send_handshake_to_server(char *, mc_id_t, multicast_cm_meta_t &, MCState &, int i);
    int get_mc_index(mc_id_t mc_id, Connection *conn) {
        kassert(multicast_connections.count(mc_id) == 1);
        int i = 0;
        for (; i < kNumMulticasts; i++) {
            if (multicast_connections[mc_id][i] == conn) break;
        }
        return i;
    }
    mc_id_t mc_id = 1;
};
;

#endif