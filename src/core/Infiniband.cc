// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2016 XSKY <haomai@xsky.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "Infiniband.h"

#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>

#include "RDMAStack.h"

#undef dout_prefix
#define dout_prefix *_dout << "Infiniband "

static const uint32_t MAX_SHARED_RX_SGE_COUNT = 1;
static const uint32_t MAX_INLINE_DATA = 0;
static const uint32_t CQ_DEPTH = 30000;

Port::Port(Context *context, struct ibv_context *ictxt, uint8_t ipn) : ctxt(ictxt), port_num(ipn), gid_idx(context->m_rdma_config_->m_gid_index_) {
    int r = ibv_query_port(ctxt, port_num, &port_attr);
    if (r == -1) {
        std::cout << typeid(this).name() << " : " << __func__ << " query port failed  " << cpp_strerror(errno) << std::endl;
        abort();
    }

    lid = port_attr.lid;
    kassert(gid_idx < port_attr.gid_tbl_len);
#ifdef HAVE_IBV_EXP
    union ibv_gid cgid;
    struct ibv_exp_gid_attr gid_attr;
    bool malformed = false;

    std::cout << typeid(this).name() << " : " << __func__ << " using experimental verbs for gid" << std::endl;

    // search for requested GID in GIDs table
    std::cout << typeid(this).name() << " : " << __func__ << " looking for local GID " << (context->m_rdma_config_->ms_async_rdma_local_gid)
              << " of type " << (context->m_rdma_config_->ms_async_rdma_roce_ver) << std::endl;
    r = sscanf(context->m_rdma_config_->ms_async_rdma_local_gid.c_str(),
               "%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx"
               ":%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx",
               &cgid.raw[0], &cgid.raw[1], &cgid.raw[2], &cgid.raw[3], &cgid.raw[4], &cgid.raw[5], &cgid.raw[6], &cgid.raw[7], &cgid.raw[8],
               &cgid.raw[9], &cgid.raw[10], &cgid.raw[11], &cgid.raw[12], &cgid.raw[13], &cgid.raw[14], &cgid.raw[15]);

    if (r != 16) {
        std::cout << typeid(this).name() << " : " << __func__ << " malformed or no GID supplied, using GID index 0" << std::endl;
        malformed = true;
    }

    gid_attr.comp_mask = IBV_EXP_QUERY_GID_ATTR_TYPE;

    for (gid_idx = 0; gid_idx < port_attr.gid_tbl_len; gid_idx++) {
        r = ibv_query_gid(ctxt, port_num, gid_idx, &gid);
        if (r) {
            std::cout << typeid(this).name() << " : " << __func__ << " query gid of port " << port_num << " index " << gid_idx << " failed  "
                      << cpp_strerror(errno) << std::endl;
            abort();
        }
        r = ibv_exp_query_gid_attr(ctxt, port_num, gid_idx, &gid_attr);
        if (r) {
            std::cout << typeid(this).name() << " : " << __func__ << " query gid attributes of port " << port_num << " index " << gid_idx
                      << " failed  " << cpp_strerror(errno) << std::endl;
            abort();
        }

        if (malformed) break;  // stay with gid_idx=0
        if ((gid_attr.type == context->m_rdma_config_->ms_async_rdma_roce_ver) && (memcmp(&gid, &cgid, 16) == 0)) {
            std::cout << typeid(this).name() << " : " << __func__ << " found at index " << gid_idx << std::endl;
            break;
        }
    }

    if (gid_idx == port_attr.gid_tbl_len) {
        std::cout << typeid(this).name() << " : " << __func__ << " Requested local GID was not found in GID table" << std::endl;
        abort();
    }
#else
    r = ibv_query_gid(ctxt, port_num, gid_idx, &gid);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " query gid failed  " << cpp_strerror(errno) << std::endl;
        abort();
    }
#endif
}

Device::Device(Context *context, ibv_device *ib_dev) : device(ib_dev), active_port(nullptr) {
    kassert(device);
    ctxt = ibv_open_device(device);
    kassert(ctxt);

    name = ibv_get_device_name(device);

    int r = ibv_query_device(ctxt, &device_attr);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to query rdma device. " << cpp_strerror(errno) << std::endl;
        abort();
    }
}

Device::Device(Context *context, struct ibv_context *ib_ctx) : device(ib_ctx->device), active_port(nullptr) {
    kassert(device);
    ctxt = ib_ctx;
    kassert(ctxt);

    name = ibv_get_device_name(device);

    int r = ibv_query_device(ctxt, &device_attr);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to query rdma device. " << cpp_strerror(errno) << std::endl;
        abort();
    }
}

void Device::binding_port(Context *context, int port_num) {
    port_cnt = get_device_attr()->phys_port_cnt;
    for (uint8_t port_id = 1; port_id <= port_cnt; ++port_id) {
        Port *port = new Port(context, ctxt, port_id);
        if (port_id == port_num && port->get_port_attr()->state == IBV_PORT_ACTIVE) {
            active_port = port;
            std::cout << typeid(this).name() << " : " << __func__ << " found active port " << static_cast<int>(port_id) << std::endl;
            break;
        } else {
            std::cout << typeid(this).name() << " : " << __func__ << " port " << port_id
                      << " is not what we want. state: " << ibv_port_state_str(port->get_port_attr()->state) << std::endl;
            delete port;
        }
    }
    if (nullptr == get_port()) {
        std::cout << typeid(this).name() << " : " << __func__ << "  port not found" << std::endl;
        kassert(get_port());
    }
}

Infiniband::QueuePair::QueuePair(Context *c, Infiniband &infiniband, ibv_qp_type type, int port, ibv_srq *srq, Infiniband::CompletionQueue *txcq,
                                 Infiniband::CompletionQueue *rxcq, uint32_t tx_queue_len, uint32_t rx_queue_len, rdma_cm_id *cid, uint32_t q_key)
    : context(c),
      infiniband(infiniband),
      type(type),
      ctxt(infiniband.device->get_context()),
      ib_physical_port(port),
      pd(infiniband.pd->pd),
      srq(srq),
      qp(NULL),
      cm_id(cid),
      peer_cm_meta{0, 0, 0, 0, 0},
      local_cm_meta{0, 0, 0, 0, 0},
      txcq(txcq),
      rxcq(rxcq),
      initial_psn(520 & PSN_MSK),
      // One extra WR for beacon
      max_send_wr(tx_queue_len + 1),
      max_recv_wr(rx_queue_len),
      q_key(q_key),
      dead(false) {
    if (type != IBV_QPT_RC && type != IBV_QPT_UD && type != IBV_QPT_RAW_PACKET) {
        std::cout << typeid(this).name() << " : " << __func__ << " invalid queue pair type" << cpp_strerror(errno) << std::endl;
        abort();
    }
}

int Infiniband::QueuePair::modify_qp_to_error(void) {
    ibv_qp_attr qpa;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_ERR;
    if (ibv_modify_qp(qp, &qpa, IBV_QP_STATE)) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to transition to ERROR state: " << cpp_strerror(errno) << std::endl;
        return -1;
    }
    std::cout << typeid(this).name() << " : " << __func__ << " transition to ERROR state successfully." << std::endl;
    return 0;
}

int Infiniband::QueuePair::modify_qp_to_rts(void) {
    // move from RTR state RTS
    ibv_qp_attr qpa;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_RTS;
    /*
     * How long to wait before retrying if packet lost or server dead.
     * Supposedly the timeout is 4.096us*2^timeout.  However, the actual
     * timeout appears to be 4.096us*2^(timeout+1), so the setting
     * below creates a 135ms timeout.
     */
    qpa.timeout = 0x12;
    // How many times to retry after timeouts before giving up.
    qpa.retry_cnt = 7;
    /*
     * How many times to retry after RNR (receiver not ready) condition
     * before giving up. Occurs when the remote side has not yet posted
     * a receive request.
     */
    qpa.rnr_retry = 7;  // 7 is infinite retry.
    qpa.sq_psn = local_cm_meta.psn;
    qpa.max_rd_atomic = 1;

    int attr_mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    int r = ibv_modify_qp(qp, &qpa, attr_mask);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to transition to RTS state: " << cpp_strerror(errno) << std::endl;
        return -1;
    }
    std::cout << typeid(this).name() << " : " << __func__ << " transition to RTS state successfully." << std::endl;
    return 0;
}

int Infiniband::QueuePair::modify_qp_to_rtr(void) {
    // move from INIT to RTR state
    ibv_qp_attr qpa;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_RTR;
    qpa.path_mtu = IBV_MTU_1024;
    qpa.dest_qp_num = peer_cm_meta.local_qpn;
    qpa.rq_psn = peer_cm_meta.psn;
    qpa.max_dest_rd_atomic = 1;
    qpa.min_rnr_timer = 0x12;
    qpa.ah_attr.is_global = 1;
    qpa.ah_attr.grh.hop_limit = 6;
    qpa.ah_attr.grh.dgid = peer_cm_meta.gid;
    qpa.ah_attr.grh.sgid_index = infiniband.get_device()->get_gid_idx();
    qpa.ah_attr.grh.traffic_class = context->m_rdma_config_->m_rdma_dscp_;
    // qpa.ah_attr.grh.flow_label = 0;

    qpa.ah_attr.dlid = peer_cm_meta.lid;
    qpa.ah_attr.sl = context->m_rdma_config_->m_rdma_sl_;
    qpa.ah_attr.src_path_bits = 0;
    qpa.ah_attr.port_num = (uint8_t)(ib_physical_port);

    std::cout << typeid(this).name() << " : " << __func__ << " Choosing gid_index " << (int)qpa.ah_attr.grh.sgid_index << ", sl "
              << (int)qpa.ah_attr.sl << std::endl;

    int attr_mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC;

    int r = ibv_modify_qp(qp, &qpa, attr_mask);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to transition to RTR state: " << cpp_strerror(errno) << std::endl;
        return -1;
    }
    std::cout << typeid(this).name() << " : " << __func__ << " transition to RTR state successfully." << std::endl;
    return 0;
}

int Infiniband::QueuePair::modify_qp_to_init(void) {
    // move from RESET to INIT state
    ibv_qp_attr qpa;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_INIT;
    qpa.pkey_index = 0;
    qpa.port_num = (uint8_t)(ib_physical_port);
    qpa.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;
    qpa.qkey = q_key;

    int mask = IBV_QP_STATE | IBV_QP_PORT;
    switch (type) {
        case IBV_QPT_RC:
            mask |= IBV_QP_ACCESS_FLAGS;
            mask |= IBV_QP_PKEY_INDEX;
            break;
        case IBV_QPT_UD:
            mask |= IBV_QP_QKEY;
            mask |= IBV_QP_PKEY_INDEX;
            break;
        case IBV_QPT_RAW_PACKET:
            break;
        default:
            abort();
    }

    if (ibv_modify_qp(qp, &qpa, mask)) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to switch to INIT state Queue Pair, qp number: " << qp->qp_num
                  << " Error: " << cpp_strerror(errno) << std::endl;
        return -1;
    }
    std::cout << typeid(this).name() << " : " << __func__ << " successfully switch to INIT state Queue Pair, qp number: " << qp->qp_num << std::endl;
    return 0;
}

int Infiniband::QueuePair::init() {
    std::cout << typeid(this).name() << " : " << __func__ << " started." << std::endl;
    ibv_qp_init_attr qpia;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&qpia, 0, sizeof(qpia));
    qpia.send_cq = txcq->get_cq();
    qpia.recv_cq = rxcq->get_cq();
    if (srq) {
        qpia.srq = srq;  // use the same shared receive queue
    } else {
        qpia.cap.max_recv_wr = max_recv_wr;
        qpia.cap.max_recv_sge = 1;
    }
    qpia.cap.max_send_wr = max_send_wr;          // max outstanding send requests
    qpia.cap.max_send_sge = 1;                   // max send scatter-gather elements
    qpia.cap.max_inline_data = MAX_INLINE_DATA;  // max bytes of immediate data on send q
    qpia.qp_type = type;                         // RC, UC, UD, or XRC
    qpia.sq_sig_all = 0;                         // only generate CQEs on requested WQEs

    if (!context->m_rdma_config_->m_use_rdma_cm_) {
        qp = ibv_create_qp(pd, &qpia);
        if (qp == NULL) {
            std::cout << typeid(this).name() << " : " << __func__ << " failed to create queue pair" << cpp_strerror(errno) << std::endl;
            if (errno == ENOMEM) {
                std::cout << typeid(this).name() << " : " << __func__
                          << " try reducing m_rdma_receive_queue_len_gth, "
                             " m_rdma_send_buffers_ or"
                             " m_rdma_buffer_size_bytes_"
                          << std::endl;
            }
            return -1;
        }
        if (modify_qp_to_init() != 0) {
            ibv_destroy_qp(qp);
            return -1;
        }
    } else {
        kassert(cm_id->verbs == pd->context);
        if (rdma_create_qp(cm_id, pd, &qpia)) {
            std::cout << typeid(this).name() << " : " << __func__ << " failed to create queue pair with rdmacm library" << cpp_strerror(errno)
                      << std::endl;
            return -1;
        }
        qp = cm_id->qp;
    }
    std::cout << typeid(this).name() << " : " << __func__ << " successfully create queue pair: "
              << "qp=" << qp << std::endl;
    local_cm_meta.local_qpn = get_local_qp_number();
    local_cm_meta.psn = get_initial_psn();
    local_cm_meta.lid = infiniband.get_lid();
    local_cm_meta.peer_qpn = 0;
    local_cm_meta.gid = infiniband.get_gid();
    if (!srq) {
        int rq_wrs = infiniband.post_chunks_to_rq(max_recv_wr, this);
        if (rq_wrs == 0) {
            std::cout << typeid(this).name() << " : " << __func__ << " intialize no SRQ Queue Pair, qp number: " << qp->qp_num
                      << " fatal error: can't post SQ WR " << std::endl;
            return -1;
        }
        std::cout << typeid(this).name() << " : " << __func__ << " initialize no SRQ Queue Pair, qp number: " << qp->qp_num << " post SQ WR "
                  << rq_wrs << std::endl;
    }
    return 0;
}

void Infiniband::QueuePair::wire_gid_to_gid(const char *wgid, ib_cm_meta_t *cm_meta_data) {
    char tmp[9];
    uint32_t v32;
    int i;

    for (tmp[8] = 0, i = 0; i < 4; ++i) {
        memcpy(tmp, wgid + i * 8, 8);
        sscanf(tmp, "%x", &v32);
        *(uint32_t *)(&cm_meta_data->gid.raw[i * 4]) = ntohl(v32);
    }
}

void Infiniband::QueuePair::gid_to_wire_gid(const ib_cm_meta_t &cm_meta_data, char wgid[]) {
    for (int i = 0; i < 4; ++i) sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(cm_meta_data.gid.raw + i * 4)));
}

/*
 * return value
 *   1: means no valid buffer read
 *   0: means got enough buffer
 * < 0: means error
 */
int Infiniband::QueuePair::recv_cm_meta(Context *context, int socket_fd) {
    std::cout << __func__ << std::endl;
    char msg[TCP_MSG_LEN];
    char gid[33];
    ssize_t r = ::read(socket_fd, &msg, sizeof(msg));
    // Drop incoming qpt

    if (r < 0) {
        r = -errno;
        std::cout << typeid(this).name() << " : " << __func__ << " got error " << r << ": " << cpp_strerror(r) << std::endl;
    } else if (r == 0) {  // valid disconnect message of length 0
        std::cout << typeid(this).name() << " : " << __func__ << " got disconnect message " << std::endl;
    } else if ((size_t)r != sizeof(msg)) {  // invalid message
        std::cout << typeid(this).name() << " : " << __func__ << " got bad length (" << r << ") "
                  << " expected (" << sizeof(msg) << ")" << std::endl;
        r = -EINVAL;
    } else {  // valid message
        uint32_t dummy1;
        uint16_t dummy2;
        sscanf(msg, "%hx:%x:%x:%x:%x:%hx:%x:%hx:%s", &(peer_cm_meta.lid), &(peer_cm_meta.local_qpn), &(peer_cm_meta.psn), &(peer_cm_meta.peer_qpn),
               &dummy1, &dummy2, &dummy1, &dummy2, gid);
        wire_gid_to_gid(gid, &peer_cm_meta);
        std::cout << typeid(this).name() << " : " << __func__ << " recevd: " << peer_cm_meta.lid << ", " << peer_cm_meta.local_qpn << ", "
                  << peer_cm_meta.psn << ", " << peer_cm_meta.peer_qpn << ", " << gid << std::endl;
    }
    return r;
}

int Infiniband::QueuePair::send_cm_meta(Context *context, int socket_fd) {
    int retry = 0;
    ssize_t r;

    char msg[TCP_MSG_LEN];
    char gid[33];
retry:
    gid_to_wire_gid(local_cm_meta, gid);
    // %04x : % 08x : % 08x : % 08x : % 08x : % 04x : % 08x : %04x : % s
    sprintf(msg, "%hx:%x:%x:%x:%x:%hx:%x:%hx:%s", local_cm_meta.lid, local_cm_meta.local_qpn, local_cm_meta.psn, local_cm_meta.peer_qpn,
            context->m_rdma_config_->m_mc_group.ip_addr_member_1, context->m_rdma_config_->m_mc_group.port_member_1,
            context->m_rdma_config_->m_mc_group.ip_addr_member_2, context->m_rdma_config_->m_mc_group.port_member_2, gid);
    std::cout << typeid(this).name() << " : " << __func__ << " sending: " << local_cm_meta.lid << ", " << local_cm_meta.local_qpn << ", "
              << local_cm_meta.psn << ", " << local_cm_meta.peer_qpn << ", " << gid << std::endl;
    r = ::write(socket_fd, msg, sizeof(msg));

    if ((size_t)r != sizeof(msg)) {
        // FIXME need to handle EAGAIN instead of retry
        if (r < 0 && (errno == EINTR || errno == EAGAIN) && retry < 3) {
            retry++;
            goto retry;
        }
        if (r < 0)
            std::cout << typeid(this).name() << " : " << __func__ << " send returned error " << errno << ": " << cpp_strerror(errno) << std::endl;
        else
            std::cout << typeid(this).name() << " : " << __func__ << " send got bad length (" << r << ") " << cpp_strerror(errno) << std::endl;
        return -errno;
    }
    return 0;
}

/**
 * Switch QP to ERROR state and then post a beacon to be able to drain all
 * WCEs and then safely destroy QP. See RDMADispatcher::handle_tx_event()
 * for details.
 *
 * \return
 *      -errno if the QueuePair can't switch to ERROR
 *      0 for success.
 */
int Infiniband::QueuePair::to_dead() {
    if (dead) return 0;

    if (modify_qp_to_error()) {
        return -1;
    }
    std::cout << typeid(this).name() << " : " << __func__ << " force trigger error state Queue Pair, qp number: " << local_cm_meta.local_qpn
              << " bound remote QueuePair, qp number: " << local_cm_meta.peer_qpn << std::endl;

    struct ibv_send_wr *bad_wr = nullptr, beacon;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&beacon, 0, sizeof(beacon));
    beacon.wr_id = BEACON_WRID;
    beacon.opcode = IBV_WR_SEND;
    beacon.send_flags = IBV_SEND_SIGNALED;
    if (ibv_post_send(qp, &beacon, &bad_wr)) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to send a beacon: " << cpp_strerror(errno) << std::endl;
        return -errno;
    }
    std::cout << typeid(this).name() << " : " << __func__ << " trigger error state Queue Pair, qp number: " << local_cm_meta.local_qpn
              << " Beacon sent " << std::endl;
    dead = true;

    return 0;
}

int Infiniband::QueuePair::get_remote_qp_number(uint32_t *rqp) const {
    ibv_qp_attr qpa;
    ibv_qp_init_attr qpia;

    int r = ibv_query_qp(qp, &qpa, IBV_QP_DEST_QPN, &qpia);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to query qp: " << cpp_strerror(errno) << std::endl;
        return -1;
    }

    if (rqp) *rqp = qpa.dest_qp_num;
    return 0;
}

/**
 * Get the remote infiniband address for this QueuePair, as set in #plumb().
 * LIDs are "local IDs" in infiniband terminology. They are short, locally
 * routable addresses.
 */
int Infiniband::QueuePair::get_remote_lid(uint16_t *lid) const {
    ibv_qp_attr qpa;
    ibv_qp_init_attr qpia;

    int r = ibv_query_qp(qp, &qpa, IBV_QP_AV, &qpia);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to query qp: " << cpp_strerror(errno) << std::endl;
        return -1;
    }

    if (lid) *lid = qpa.ah_attr.dlid;
    return 0;
}

/**
 * Get the state of a QueuePair.
 */
int Infiniband::QueuePair::get_state() const {
    ibv_qp_attr qpa;
    ibv_qp_init_attr qpia;

    int r = ibv_query_qp(qp, &qpa, IBV_QP_STATE, &qpia);
    if (r) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to get state: " << cpp_strerror(errno) << std::endl;
        return -1;
    }
    return qpa.qp_state;
}

Infiniband::CompletionChannel::CompletionChannel(Context *c, Infiniband &ib)
    : context(c), infiniband(ib), channel(NULL), cq(NULL), cq_events_that_need_ack(0) {}

Infiniband::CompletionChannel::~CompletionChannel() {
    if (channel) {
        int r = ibv_destroy_comp_channel(channel);
        if (r < 0) std::cout << typeid(this).name() << " : " << __func__ << " failed to destroy cc: " << cpp_strerror(errno) << std::endl;
        kassert(r == 0);
    }
}

int Infiniband::CompletionChannel::init() {
    std::cout << typeid(this).name() << " : " << __func__ << " started." << std::endl;
    channel = ibv_create_comp_channel(infiniband.device->get_context());
    if (!channel) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to create receive completion channel: " << cpp_strerror(errno) << std::endl;
        return -1;
    }
    int rc = Network::NetHandler::set_nonblock(channel->fd);
    if (rc < 0) {
        ibv_destroy_comp_channel(channel);
        return -1;
    }
    return 0;
}

void Infiniband::CompletionChannel::ack_events() {
    ibv_ack_cq_events(cq, cq_events_that_need_ack);
    cq_events_that_need_ack = 0;
}

bool Infiniband::CompletionChannel::get_cq_event() {
    ibv_cq *cq = NULL;
    void *ev_ctx;
    if (ibv_get_cq_event(channel, &cq, &ev_ctx)) {
        if (errno != EAGAIN && errno != EINTR)
            std::cout << typeid(this).name() << " : " << __func__ << " failed to retrieve CQ event: " << cpp_strerror(errno) << std::endl;
        return false;
    }

    /* accumulate number of cq events that need to
     *    * be acked, and periodically ack them
     *       */
    if (++cq_events_that_need_ack == MAX_ACK_EVENT) {
        std::cout << typeid(this).name() << " : " << __func__ << " ack aq events." << std::endl;
        ibv_ack_cq_events(cq, MAX_ACK_EVENT);
        cq_events_that_need_ack = 0;
    }

    return true;
}

Infiniband::CompletionQueue::~CompletionQueue() {
    if (cq) {
        int r = ibv_destroy_cq(cq);
        if (r < 0) std::cout << typeid(this).name() << " : " << __func__ << " failed to destroy cq: " << cpp_strerror(errno) << std::endl;
        kassert(r == 0);
    }
}

int Infiniband::CompletionQueue::init() {
    cq = ibv_create_cq(infiniband.device->get_context(), queue_depth, this, channel->get_channel(), 0);
    if (!cq) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to create receive completion queue: " << cpp_strerror(errno) << std::endl;
        return -1;
    }

    if (ibv_req_notify_cq(cq, 0)) {
        std::cout << typeid(this).name() << " : " << __func__ << " ibv_req_notify_cq failed: " << cpp_strerror(errno) << std::endl;
        ibv_destroy_cq(cq);
        cq = nullptr;
        return -1;
    }

    channel->bind_cq(cq);
    std::cout << typeid(this).name() << " : " << __func__ << " successfully create cq=" << cq << std::endl;
    return 0;
}

int Infiniband::CompletionQueue::rearm_notify(bool solicite_only) {
    // std::cout << typeid(this).name() << " : " << __func__ << " started." << std::endl;
    int r = ibv_req_notify_cq(cq, 0);
    if (r < 0) std::cout << typeid(this).name() << " : " << __func__ << " failed to notify cq: " << cpp_strerror(errno) << std::endl;
    return r;
}

int Infiniband::CompletionQueue::poll_cq(int num_entries, ibv_wc *ret_wc_array) {
    int r = ibv_poll_cq(cq, num_entries, ret_wc_array);
    if (unlikely(r < 0)) {
        std::cout << typeid(this).name() << " : " << __func__ << " poll_completion_queue occur met error: " << cpp_strerror(errno) << std::endl;
        return -1;
    }
    return r;
}

Infiniband::ProtectionDomain::ProtectionDomain(Context *context, Device *device) : pd(ibv_alloc_pd(device->get_context())) {
    if (pd == NULL) {
        std::cout << typeid(this).name() << " : " << __func__ << " failed to allocate infiniband protection domain: " << cpp_strerror(errno)
                  << std::endl;
        abort();
    }
}

Infiniband::ProtectionDomain::~ProtectionDomain() { ibv_dealloc_pd(pd); }

uint64_t Infiniband::MemoryManager::Chunk::log_id = 0;

Infiniband::MemoryManager::Chunk::Chunk(ibv_mr *m, uint32_t bytes, char *buffer, uint32_t offset, uint32_t bound, uint32_t lkey, QueuePair *qp)
    : mr(m), qp(qp), lkey(lkey), bytes(bytes), offset(offset), bound(bound), buffer(buffer) {}

Infiniband::MemoryManager::Chunk::~Chunk() {}

uint32_t Infiniband::MemoryManager::Chunk::get_offset() { return offset; }

uint32_t Infiniband::MemoryManager::Chunk::get_size() const { return bound - offset; }

void Infiniband::MemoryManager::Chunk::prepare_read(uint32_t b) {
    offset = 0;
    bound = b;
}

uint32_t Infiniband::MemoryManager::Chunk::get_bound() { return bound; }

uint32_t Infiniband::MemoryManager::Chunk::read(char *buf, uint32_t len) {
    uint32_t left = get_size();
    uint32_t read_len = left <= len ? left : len;
    memcpy(buf, buffer + offset, read_len);
    offset += read_len;
    return read_len;
}

uint32_t Infiniband::MemoryManager::Chunk::write(char *buf, uint32_t len) {
    uint32_t write_len = (bytes - offset) <= len ? (bytes - offset) : len;
    memcpy(buffer + offset, buf, write_len);
    offset += write_len;
    return write_len;
}

bool Infiniband::MemoryManager::Chunk::full() { return offset == bytes; }

void Infiniband::MemoryManager::Chunk::reset_read_chunk() {
    offset = 0;
    bound = 0;
}

void Infiniband::MemoryManager::Chunk::reset_write_chunk() {
    offset = 0;
    bound = bytes;
}

Infiniband::MemoryManager::Cluster::Cluster(MemoryManager &m, uint32_t s) : manager(m), buffer_size(s) {}

Infiniband::MemoryManager::Cluster::~Cluster() {
    int r = ibv_dereg_mr(chunk_base->mr);
    kassert(r == 0);
    const auto chunk_end = chunk_base + num_chunk;
    for (auto chunk = chunk_base; chunk != chunk_end; chunk++) {
        chunk->~Chunk();
    }

    ::free(chunk_base);
    manager.free(base);
}

int Infiniband::MemoryManager::Cluster::fill(uint32_t num) {
    kassert(!base);
    num_chunk = num;
    uint32_t bytes = buffer_size * num;

    base = (char *)manager.malloc(bytes);
    end = base + bytes;
    kassert(base);
    chunk_base = static_cast<Chunk *>(::malloc(sizeof(Chunk) * num));
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(static_cast<void *>(chunk_base), 0, sizeof(Chunk) * num);
    free_chunks.reserve(num);
    ibv_mr *m = ibv_reg_mr(manager.pd->pd, base, bytes, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);
    kassert(m);
    Chunk *chunk = chunk_base;
    for (uint32_t offset = 0; offset < bytes; offset += buffer_size) {
        new (chunk) Chunk(m, buffer_size, base + offset, 0, buffer_size, m->lkey);
        free_chunks.push_back(chunk);
        chunk++;
    }
    return 0;
}

void Infiniband::MemoryManager::Cluster::take_back(std::vector<Chunk *> &ck) {
    std::lock_guard<std::mutex> l{lock};
    for (auto c : ck) {
        c->reset_write_chunk();
        free_chunks.push_back(c);
    }
}

int Infiniband::MemoryManager::Cluster::get_buffers(std::vector<Chunk *> &chunks, size_t block_size) {
    std::lock_guard<std::mutex> l{lock};
    // krayecho: this is a fluky calculation
    uint32_t chunk_buffer_number = (block_size + buffer_size - 1) / buffer_size;
    chunk_buffer_number = free_chunks.size() < chunk_buffer_number ? free_chunks.size() : chunk_buffer_number;
    uint32_t r = 0;

    for (r = 0; r < chunk_buffer_number; ++r) {
        chunks.push_back(free_chunks.back());
        free_chunks.pop_back();
    }
    return r;
}

bool Infiniband::MemoryManager::MemPoolContext::can_alloc(unsigned nbufs) {
    /* unlimited */
    if (manager->context->m_rdma_config_->m_rdma_receive_buffers_bytes_ <= 0) return true;

    if (n_bufs_allocated + nbufs > (unsigned)manager->context->m_rdma_config_->m_rdma_receive_buffers_bytes_) {
        std::cout << typeid(this).name() << " : " << __func__ << " WARNING: OUT OF RX BUFFERS: allocated: " << n_bufs_allocated
                  << " requested: " << nbufs << " limit: " << manager->context->m_rdma_config_->m_rdma_receive_buffers_bytes_ << std::endl;
        return false;
    }

    return true;
}

void Infiniband::MemoryManager::MemPoolContext::set_stat_logger(PerfCounters *logger) {
    perf_logger = logger;
    if (perf_logger != nullptr) perf_logger->set(l_msgr_rdma_rx_bufs_total, n_bufs_allocated);
}

void Infiniband::MemoryManager::MemPoolContext::update_stats(int nbufs) {
    n_bufs_allocated += nbufs;

    if (!perf_logger) return;

    if (nbufs > 0) {
        perf_logger->inc(l_msgr_rdma_rx_bufs_total, nbufs);
    } else {
        perf_logger->dec(l_msgr_rdma_rx_bufs_total, -nbufs);
    }
}

void *Infiniband::MemoryManager::mem_pool::slow_malloc() {
    // this will trigger pool expansion via PoolAllocator::malloc()
    return PoolAllocator::with_context(ctx, [this] { return boost::pool<PoolAllocator>::malloc(); });
}

Infiniband::MemoryManager::MemPoolContext *Infiniband::MemoryManager::PoolAllocator::g_ctx = nullptr;

// lock is taken by mem_pool::slow_malloc()
std::mutex &Infiniband::MemoryManager::PoolAllocator::get_lock() {
    static std::mutex lock;
    return lock;
}

char *Infiniband::MemoryManager::PoolAllocator::malloc(const size_type block_size) {
    kassert(g_ctx);
    MemoryManager *manager = g_ctx->manager;
    Context *context = manager->context;
    size_t chunk_buffer_size = sizeof(Chunk) + context->m_rdma_config_->m_rdma_buffer_size_bytes_;
    size_t chunk_buffer_number = block_size / chunk_buffer_size;

    if (!g_ctx->can_alloc(chunk_buffer_number)) return NULL;

    mem_info *minfo = static_cast<mem_info *>(manager->malloc(block_size + sizeof(mem_info)));
    if (!minfo) {
        std::cout << typeid(PoolAllocator).name() << " : " << __func__ << " failed to allocate " << chunk_buffer_number
                  << " buffers "
                     " Its block size is : "
                  << block_size + sizeof(mem_info) << std::endl;
        return NULL;
    }

    minfo->mr = ibv_reg_mr(manager->pd->pd, minfo->chunks, block_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);
    if (minfo->mr == NULL) {
        std::cout << typeid(PoolAllocator).name() << " : " << __func__ << " failed to do rdma memory registration " << block_size
                  << " bytes. "
                     " relase allocated memory now."
                  << std::endl;
        manager->free(minfo);
        return NULL;
    }

    minfo->nbufs = chunk_buffer_number;
    // save this chunk context
    minfo->ctx = g_ctx;

    // note that the memory can be allocated before perf logger is set
    g_ctx->update_stats(chunk_buffer_number);

    /* initialize chunks */
    Chunk *chunk = minfo->chunks;
    for (unsigned i = 0; i < chunk_buffer_number; i++) {
        new (chunk) Chunk(minfo->mr, context->m_rdma_config_->m_rdma_buffer_size_bytes_, chunk->data, 0, 0, minfo->mr->lkey);
        chunk = reinterpret_cast<Chunk *>(reinterpret_cast<char *>(chunk) + chunk_buffer_size);
    }

    return reinterpret_cast<char *>(minfo->chunks);
}

void Infiniband::MemoryManager::PoolAllocator::free(char *const block) {
    mem_info *m;
    std::lock_guard<std::mutex> l{get_lock()};

    Chunk *mem_info_chunk = reinterpret_cast<Chunk *>(block);
    m = reinterpret_cast<mem_info *>(reinterpret_cast<char *>(mem_info_chunk) - offsetof(mem_info, chunks));
    m->ctx->update_stats(-m->nbufs);
    ibv_dereg_mr(m->mr);
    m->ctx->manager->free(m);
}

Infiniband::MemoryManager::MemoryManager(Context *c, Device *d, ProtectionDomain *p)
    : context(c),
      device(d),
      pd(p),
      rxbuf_pool_ctx(this),
      rxbuf_pool(&rxbuf_pool_ctx, sizeof(Chunk) + c->m_rdma_config_->m_rdma_buffer_size_bytes_,
                 c->m_rdma_config_->m_rdma_receive_buffers_bytes_ > 0 ?
                                                                      // if possible make initial pool size 2 * receive_queue_len
                                                                      // that way there will be no pool expansion upon receive of the
                                                                      // first packet.
                     (c->m_rdma_config_->m_rdma_receive_buffers_bytes_ < 2 * c->m_rdma_config_->m_rdma_receive_queue_len_
                          ? c->m_rdma_config_->m_rdma_receive_buffers_bytes_
                          : 2 * c->m_rdma_config_->m_rdma_receive_queue_len_)
                                                                      :
                                                                      // rx pool is infinite, we can set any initial size that we want
                     2 * c->m_rdma_config_->m_rdma_receive_queue_len_,
                 device->get_device_attr()->max_mr_size / (sizeof(Chunk) + context->m_rdma_config_->m_rdma_buffer_size_bytes_)) {}

Infiniband::MemoryManager::~MemoryManager() {
    if (send_buffers) delete send_buffers;
}

void *Infiniband::MemoryManager::huge_pages_malloc(size_t size) {
    size_t real_size = ALIGN_TO_PAGE_2MB(size) + HUGE_PAGE_SIZE_2MB;
    char *ptr = (char *)mmap(NULL, real_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB, -1, 0);
    if (ptr == MAP_FAILED) {
        ptr = (char *)std::malloc(real_size);
        if (ptr == NULL) return NULL;
        real_size = 0;
    }
    *((size_t *)ptr) = real_size;
    return ptr + HUGE_PAGE_SIZE_2MB;
}

void Infiniband::MemoryManager::huge_pages_free(void *ptr) {
    if (ptr == NULL) return;
    void *real_ptr = (char *)ptr - HUGE_PAGE_SIZE_2MB;
    size_t real_size = *((size_t *)real_ptr);
    kassert(real_size % HUGE_PAGE_SIZE_2MB == 0);
    if (real_size != 0)
        munmap(real_ptr, real_size);
    else
        std::free(real_ptr);
}

void *Infiniband::MemoryManager::malloc(size_t size) {
    if (context->m_rdma_config_->m_rdma_enable_hugepage_)
        return huge_pages_malloc(size);
    else
        return std::malloc(size);
}

void Infiniband::MemoryManager::free(void *ptr) {
    if (context->m_rdma_config_->m_rdma_enable_hugepage_)
        huge_pages_free(ptr);
    else
        std::free(ptr);
}

void Infiniband::MemoryManager::create_tx_pool(uint32_t size, uint32_t tx_num) {
    kassert(device);
    kassert(pd);

    send_buffers = new Cluster(*this, size);
    send_buffers->fill(tx_num);
}

void Infiniband::MemoryManager::return_tx(std::vector<Chunk *> &chunks) { send_buffers->take_back(chunks); }

int Infiniband::MemoryManager::get_send_buffers(std::vector<Chunk *> &c, size_t bytes) { return send_buffers->get_buffers(c, bytes); }

static std::atomic<bool> init_prereq = {false};

void Infiniband::verify_prereq(Context *context) {
    int rc = 0;
    std::cout << typeid(Infiniband).name() << " : " << __func__
              << " ms_async_rdma_enable_hugepage value is: " << context->m_rdma_config_->m_rdma_enable_hugepage_ << std::endl;
    if (context->m_rdma_config_->m_rdma_enable_hugepage_) {
        rc = setenv("RDMAV_HUGEPAGES_SAFE", "1", 1);
        std::cout << typeid(Infiniband).name() << " : " << __func__ << " RDMAV_HUGEPAGES_SAFE is set as: " << getenv("RDMAV_HUGEPAGES_SAFE")
                  << std::endl;
        if (rc) {
            std::cout << typeid(Infiniband).name() << " : " << __func__
                      << " failed to export RDMA_HUGEPAGES_SAFE. On RDMA must be exported before using huge pages. Application aborts." << std::endl;
            abort();
        }
    }

    // On RDMA MUST be called before fork
    rc = ibv_fork_init();
    if (rc) {
        std::cout << typeid(Infiniband).name() << " : " << __func__
                  << " failed to call ibv_for_init(). On RDMA must be called before fork. Application aborts." << std::endl;
        abort();
    }

    // Check ulimit
    struct rlimit limit;
    getrlimit(RLIMIT_MEMLOCK, &limit);
    if (limit.rlim_cur != RLIM_INFINITY || limit.rlim_max != RLIM_INFINITY) {
        std::cout
            << typeid(Infiniband).name() << " : " << __func__
            << "!!! WARNING !!! For RDMA to work properly user memlock (ulimit -l) must be big enough to allow large amount of registered memory."
               " We recommend setting this parameter to infinity"
            << std::endl;
    }
    init_prereq = true;
}

Infiniband::Infiniband(Context *context)
    : context(context), device_name(context->m_rdma_config_->m_rdma_device_name_), port_num(context->m_rdma_config_->m_rdma_port_num_) {
    if (!init_prereq) verify_prereq(context);
    std::cout << typeid(this).name() << " : " << __func__ << " constructing Infiniband..." << std::endl;
}

void Infiniband::init() {
    std::lock_guard<std::mutex> l{lock};

    if (initialized) return;

    device_list = new DeviceList(context);
    initialized = true;

    device = device_list->get_device(device_name.c_str());
    kassert(device);
    device->binding_port(context, port_num);
    ib_physical_port = device->get_port()->get_port_num();
    pd = new ProtectionDomain(context, device);
    int rs = Network::NetHandler::set_nonblock(device->get_context()->async_fd);
    kassert(rs == 0);

    support_srq = context->m_rdma_config_->m_rdma_support_srq_;
    if (support_srq) {
        kassert(device->get_device_attr()->max_srq);
        rx_queue_len = device->get_device_attr()->max_srq_wr;
    } else
        rx_queue_len = device->get_device_attr()->max_qp_wr;
    if (rx_queue_len > context->m_rdma_config_->m_rdma_receive_queue_len_) {
        rx_queue_len = context->m_rdma_config_->m_rdma_receive_queue_len_;
        std::cout << typeid(this).name() << " : " << __func__ << " assigning: " << rx_queue_len << " receive buffers" << std::endl;
    } else {
        std::cout << typeid(this).name() << " : " << __func__ << " using the max allowed receive buffers: " << rx_queue_len << std::endl;
    }

    // check for the misconfiguration
    if (context->m_rdma_config_->m_rdma_receive_buffers_bytes_ > 0 &&
        rx_queue_len > (unsigned)context->m_rdma_config_->m_rdma_receive_buffers_bytes_) {
        std::cout << typeid(this).name() << " : " << __func__ << " rdma_receive_queue_len (" << rx_queue_len << ") > m_rdma_receive_buffers_bytes_("
                  << context->m_rdma_config_->m_rdma_receive_buffers_bytes_ << ")." << std::endl;
        abort();
    }

    // Keep extra one WR for a beacon to indicate all WCEs were consumed
    tx_queue_len = device->get_device_attr()->max_qp_wr - 1;
    if (tx_queue_len > context->m_rdma_config_->m_rdma_send_queeu_len_) {
        tx_queue_len = context->m_rdma_config_->m_rdma_send_queeu_len_;
        std::cout << typeid(this).name() << " : " << __func__ << " assigning: " << tx_queue_len << " send buffers" << std::endl;
    } else {
        std::cout << typeid(this).name() << " : " << __func__ << " using the max allowed send buffers: " << tx_queue_len << std::endl;
    }

    // check for the memory region size misconfiguration
    if ((uint64_t)context->m_rdma_config_->m_rdma_buffer_size_bytes_ * tx_queue_len > device->get_device_attr()->max_mr_size) {
        std::cout << typeid(this).name() << " : " << __func__ << " Out of max memory region size " << std::endl;
        abort();
    }

    std::cout << typeid(this).name() << " : " << __func__ << " device allow " << device->get_device_attr()->max_cqe << " completion entries"
              << std::endl;

    memory_manager = new MemoryManager(context, device, pd);
    memory_manager->create_tx_pool(context->m_rdma_config_->m_rdma_buffer_size_bytes_, tx_queue_len);

    if (support_srq) {
        srq = create_shared_receive_queue(rx_queue_len, MAX_SHARED_RX_SGE_COUNT);
        post_chunks_to_rq(rx_queue_len, NULL);  // add to srq
    }
}

Infiniband::~Infiniband() {
    if (!initialized) return;
    if (support_srq) ibv_destroy_srq(srq);
    delete memory_manager;
    delete pd;
    delete device_list;
}

/**
 * Create a shared receive queue. This basically wraps the verbs call.
 *
 * \param[in] max_wr
 *      The max number of outstanding work requests in the SRQ.
 * \param[in] max_sge
 *      The max number of scatter elements per WR.
 * \return
 *      A valid ibv_srq pointer, or NULL on error.
 */
ibv_srq *Infiniband::create_shared_receive_queue(uint32_t max_wr, uint32_t max_sge) {
    ibv_srq_init_attr sia;
    // FIPS zeroization audit 20191115: this memset is not security related.
    memset(&sia, 0, sizeof(sia));
    sia.srq_context = device->get_context();
    sia.attr.max_wr = max_wr;
    sia.attr.max_sge = max_sge;
    return ibv_create_srq(pd->pd, &sia);
}

int Infiniband::get_tx_buffers(std::vector<Chunk *> &c, size_t bytes) { return memory_manager->get_send_buffers(c, bytes); }

/**
 * Create a new QueuePair. This factory should be used in preference to
 * the QueuePair constructor directly, since this lets derivatives of
 * Infiniband, e.g. MockInfiniband (if it existed),
 * return mocked out QueuePair derivatives.
 *
 * \return
 *      QueuePair on success or NULL if init fails
 * See QueuePair::QueuePair for parameter documentation.
 */
Infiniband::QueuePair *Infiniband::create_queue_pair(Context *context, CompletionQueue *tx, CompletionQueue *rx, ibv_qp_type type,
                                                     struct rdma_cm_id *cm_id) {
    Infiniband::QueuePair *qp = new QueuePair(context, *this, type, ib_physical_port, srq, tx, rx, tx_queue_len, rx_queue_len, cm_id);
    if (qp->init()) {
        delete qp;
        return NULL;
    }
    return qp;
}

int Infiniband::post_chunks_to_rq(int rq_wr_num, QueuePair *qp) {
    int ret = 0;
    Chunk *chunk = nullptr;

    ibv_recv_wr *rx_work_request = static_cast<ibv_recv_wr *>(::calloc(rq_wr_num, sizeof(ibv_recv_wr)));
    ibv_sge *isge = static_cast<ibv_sge *>(::calloc(rq_wr_num, sizeof(ibv_sge)));
    kassert(rx_work_request);
    kassert(isge);

    int i = 0;
    while (i < rq_wr_num) {
        chunk = get_memory_manager()->get_rx_buffer();
        if (chunk == nullptr) {
            std::cout << typeid(this).name() << " : " << __func__ << " WARNING: out of memory. Request " << rq_wr_num << " rx buffers. Only get " << i
                      << " rx buffers." << std::endl;
            if (i == 0) {
                ::free(rx_work_request);
                ::free(isge);
                return 0;
            }
            break;  // get some buffers, so we need post them to recevie queue
        }

        isge[i].addr = reinterpret_cast<uint64_t>(chunk->data);
        isge[i].length = chunk->bytes;
        isge[i].lkey = chunk->lkey;

        rx_work_request[i].wr_id = reinterpret_cast<uint64_t>(chunk);  // assign chunk address as work request id

        if (i != 0) {
            rx_work_request[i - 1].next = &rx_work_request[i];
        }
        rx_work_request[i].sg_list = &isge[i];
        rx_work_request[i].num_sge = 1;

        if (qp && !qp->get_srq()) {
            chunk->set_qp(qp);
            qp->add_rq_wr(chunk);
        }
        i++;
    }
    kassert(i == rq_wr_num);
    rx_work_request[i - 1].next = nullptr;
    ibv_recv_wr *badworkrequest = nullptr;
    if (support_srq) {
        ret = ibv_post_srq_recv(srq, rx_work_request, &badworkrequest);
    } else {
        kassert(qp);
        ret = ibv_post_recv(qp->get_qp(), rx_work_request, &badworkrequest);
    }

    if (unlikely(ret != 0)) {
        std::cout << "ibv_post_recv error ret:" << ret << " errno " << errno << " " << strerror(errno) << std::endl;
        while (badworkrequest != &rx_work_request[rq_wr_num - 1]) {
            Infiniband::Chunk *chunk = reinterpret_cast<Infiniband::Chunk *>(badworkrequest->wr_id);
            qp->remove_rq_wr(chunk);
            chunk->clear_qp();
            get_memory_manager()->release_rx_buffer(chunk);
            badworkrequest = badworkrequest->next;
            i--;
            kassert(i >= 0);
        }
    }

    ::free(rx_work_request);
    ::free(isge);

    // kassert(badworkrequest == nullptr && ret == 0);
    return i;
}

Infiniband::CompletionChannel *Infiniband::create_comp_channel(Context *c) {
    Infiniband::CompletionChannel *cc = new Infiniband::CompletionChannel(c, *this);
    if (cc->init()) {
        delete cc;
        return NULL;
    }
    return cc;
}

Infiniband::CompletionQueue *Infiniband::create_comp_queue(Context *context, CompletionChannel *cc) {
    Infiniband::CompletionQueue *cq = new Infiniband::CompletionQueue(context, *this, CQ_DEPTH, cc);
    if (cq->init()) {
        delete cq;
        return NULL;
    }
    return cq;
}

Infiniband::QueuePair::~QueuePair() {
    std::cout << typeid(this).name() << " : " << __func__ << " destroy Queue Pair, qp number: " << qp->qp_num << " left SQ WR " << recv_queue.size()
              << std::endl;
    if (qp) {
        std::cout << typeid(this).name() << " : " << __func__ << " destroy qp=" << qp << std::endl;
        kassert(!ibv_destroy_qp(qp));
    }

    for (auto &chunk : recv_queue) {
        infiniband.get_memory_manager()->release_rx_buffer(chunk);
    }
    recv_queue.clear();
}

/**
 * Given a string representation of the `status' field from Verbs
 * struct `ibv_wc'.
 *
 * \param[in] status
 *      The integer status obtained in ibv_wc.status.
 * \return
 *      A string corresponding to the given status.
 */
const char *Infiniband::wc_status_to_string(int status) {
    static const char *lookup[] = {
        "SUCCESS",           "LOC_LEN_ERR",       "LOC_QP_OP_ERR",    "LOC_EEC_OP_ERR",     "LOC_PROT_ERR",   "WR_FLUSH_ERR",
        "MW_BIND_ERR",       "BAD_RESP_ERR",      "LOC_ACCESS_ERR",   "REM_INV_REQ_ERR",    "REM_ACCESS_ERR", "REM_OP_ERR",
        "RETRY_EXC_ERR",     "RNR_RETRY_EXC_ERR", "LOC_RDD_VIOL_ERR", "REM_INV_RD_REQ_ERR", "REM_ABORT_ERR",  "INV_EECN_ERR",
        "INV_EEC_STATE_ERR", "FATAL_ERR",         "RESP_TIMEOUT_ERR", "GENERAL_ERR"};

    if (status < IBV_WC_SUCCESS || status > IBV_WC_GENERAL_ERR) return "<status out of range!>";
    return lookup[status];
}

const char *Infiniband::qp_state_string(int status) {
    switch (status) {
        case IBV_QPS_RESET:
            return "IBV_QPS_RESET";
        case IBV_QPS_INIT:
            return "IBV_QPS_INIT";
        case IBV_QPS_RTR:
            return "IBV_QPS_RTR";
        case IBV_QPS_RTS:
            return "IBV_QPS_RTS";
        case IBV_QPS_SQD:
            return "IBV_QPS_SQD";
        case IBV_QPS_SQE:
            return "IBV_QPS_SQE";
        case IBV_QPS_ERR:
            return "IBV_QPS_ERR";
        default:
            return " out of range.";
    }
}
