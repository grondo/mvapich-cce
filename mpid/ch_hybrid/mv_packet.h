/* Copyright (c) 2002-2010, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT_MVAPICH in the top level MPICH directory.
 *
 */

#ifndef _MV_PACKET_H
#define _MV_PACKET_H

#include "ibverbs_header.h"
#include "mv_param.h"


#define MAX_SRQS 10

typedef void *request_id_t;
typedef uint16_t packet_sequence_t;

typedef struct {
    uint8_t has_header;
    packet_sequence_t seqnum;
    uint32_t src_rank;
} mvdev_packet_mheader;

typedef struct {
    uint8_t has_header;
    packet_sequence_t seqnum;
    uint32_t src_rank;
} mvdev_packet_mheader32;


typedef struct {
    uint8_t type;
    uint16_t last_recv; 
} mvdev_packet_header;

typedef struct {
    int context;                /* context id -- identifies communicator */
    int tag;                    /* MPI message tag. */
    int data_length;            /* how many bytes in MPI message (not header).  */
    int src_lrank;
} mvdev_packet_envelope;

typedef enum {
    MVDEV_TRANSPORT_RC = 1,
    MVDEV_TRANSPORT_XRC,
    MVDEV_TRANSPORT_UD,
    MVDEV_TRANSPORT_RCFP,
} mvdev_transport;

typedef enum {
    MVDEV_PROTOCOL_EAGER = 1,
    MVDEV_PROTOCOL_R3,

    MVDEV_PROTOCOL_UD_ZCOPY, 

    MVDEV_PROTOCOL_UNREL_RPUT,
    MVDEV_PROTOCOL_REL_RPUT,
    MVDEV_PROTOCOL_RC_RGET,
    MVDEV_PROTOCOL_RENDEZVOUS_UNSPECIFIED,

    MVDEV_PROTOCOL_SMP_SHORT,
    MVDEV_PROTOCOL_SMP_RNDV,
} mvdev_protocol_t;

typedef struct {
    mvdev_packet_header header;
    void * request_id;
    void * buffer;
    uint32_t rkey;
} mvdev_packet_rcfp_addr;

typedef struct {
    uint16_t lid;
    uint32_t qpn;
} mvdev_qp_setup_info;

typedef struct {
    mvdev_packet_header header;
    mvdev_packet_envelope envelope;
} mvdev_packet_eager_start;

typedef struct {
    mvdev_packet_header header;
} mvdev_packet_eager_next;

typedef struct {
    mvdev_packet_header header; 
    mvdev_packet_envelope envelope;
    request_id_t sreq;
    int protocol;

    /* these are required for RC */
    void *buffer_address;
    uint32_t memhandle_rkey;
} mvdev_packet_rendezvous_start;

typedef struct {
    mvdev_packet_header header; 
    request_id_t rreq;          /* identifies receive handle */
    request_id_t sreq;          /* identifies send handle (echoed) */
    int protocol;               /* explicitly specify protocol */

    /* for UD Zcopy */
    uint32_t rndv_qpn;
    uint32_t start_seq;

    /* RC RPUT */
    void *buffer_address; 
    uint32_t memhandle_rkey;

} mvdev_packet_rendezvous_reply;

typedef struct {
    mvdev_packet_header header;
    request_id_t rreq;
    int tag;
} mvdev_packet_rc_rput_finish;

typedef struct {
    mvdev_packet_header header;
    request_id_t sreq;
} mvdev_packet_rput_ack;

typedef struct {
    mvdev_packet_header header;
    request_id_t rreq;
} mvdev_packet_ud_zcopy_finish;

typedef struct {
    mvdev_packet_header header;
    request_id_t sreq;
} mvdev_packet_ud_zcopy_ack;

typedef struct {
    mvdev_packet_header header;
} mvdev_packet_basic;

#define mvdev_packet_ack        mvdev_packet_basic

typedef enum {
   MVDEV_CH_UD_RQ,
   MVDEV_CH_RC_SRQ,
   MVDEV_CH_RC_MULT_SRQ,
   MVDEV_CH_XRC_SINGLE_MULT_SRQ,
   MVDEV_CH_XRC_SHARED_MULT_SRQ,
   MVDEV_CH_XRC_SRQN,
   MVDEV_CH_RC_RQ,
   MVDEV_CH_RC_FP,
} mvdev_conn_reqtype;

typedef struct {
    mvdev_packet_header header;
    mvdev_conn_reqtype req_type;
    void * target_request_id;
    void * initiator_request_id;
} mvdev_packet_conn_pkt;

typedef struct {
    mvdev_packet_header header;
    mvdev_conn_reqtype req_type;
    void * target_request_id;
    void * initiator_request_id;
    int num_srqs;
    uint32_t srqn[MAX_NUM_HCAS][MAX_SRQS];
    uint32_t buffer_size[MAX_SRQS];
} mvdev_packet_conn_pkt_srqn;

typedef struct {
    /* which hca to setup on (-1 == all) */
    int hca_num;
    mvdev_qp_setup_info setup_info[MAX_NUM_HCAS];
    int buf_size;
    int inline_size;
} mvdev_setup_srq;

typedef struct {
    /* which hca to setup on (-1 == all) */
    int hca_num;
    mvdev_qp_setup_info setup_info[MAX_NUM_HCAS];
    int buf_size;
    int inline_size;
    int credits;
} mvdev_setup_rq;

typedef struct {
    mvdev_packet_header header;
    mvdev_conn_reqtype req_type;
    void * target_request_id;
    void * initiator_request_id;
    uint8_t num_srqs;
    mvdev_setup_srq srq_setup[MAX_SRQS];
} mvdev_packet_conn_pkt_srq;

typedef struct {
    mvdev_packet_header header;
    mvdev_conn_reqtype req_type;
    void * target_request_id;
    void * initiator_request_id;
    mvdev_setup_rq rq_setup;
} mvdev_packet_conn_pkt_rq;

#define mvdev_packet_conn_pkt_xrc   mvdev_packet_conn_pkt_rq 

typedef struct {
    mvdev_packet_header header;
    request_id_t rreq;
} mvdev_packet_r3_data;

typedef enum {
    MVDEV_PACKET_EAGER_START = 1,
    MVDEV_PACKET_EAGER_NEXT,
    MVDEV_PACKET_FP_CACHED_EAGER,

    MVDEV_PACKET_RENDEZVOUS_START,
    MVDEV_PACKET_RENDEZVOUS_REPLY,

    MVDEV_PACKET_R3_DATA,

    MVDEV_PACKET_UD_ZCOPY_FINISH,
    MVDEV_PACKET_UD_ZCOPY_ACK,
    MVDEV_PACKET_UD_ZCOPY_NAK,

    MVDEV_PACKET_RPUT_FINISH,
    MVDEV_PACKET_RPUT_ACK,

    MVDEV_PACKET_CH_REQUEST,
    MVDEV_PACKET_CH_REPLY,
    MVDEV_PACKET_CH_ACK,

    MVDEV_PACKET_CH_RCFP_ADDR,

    MVDEV_PACKET_ACK,
    MVDEV_PACKET_RDMAWRITE,
} mvdev_packet_type;



#define PACKET_SET_HEADER(p, c, t) {                               \
        p->header.type = t;                                        \
        p->header.last_recv = c->seqnum_next_toack;                \
}

#define PACKET_SET_ENVELOPE(p, ctx, tag, len, src) {                \
        p->envelope.context     = ctx;                                  \
        p->envelope.tag         = tag;                                  \
        p->envelope.data_length = len;                                  \
        p->envelope.src_lrank = src; \
}

#define PACKET_SET_CREDITS(v, c) {                           \
    if(v->header) {                                             \
        mvdev_packet_header * p =                               \
                (mvdev_packet_header *) v->header_ptr;          \
        p->last_recv = c->seqnum_next_toack;                    \
        c->ud_remote_credit = 0;                                \
        c->rcfp_remote_credit = 0;                              \
        c->rcrq_remote_credit = 0;                              \
    }                                                           \
}


#define RESET_CREDITS(v, c) {                           \
    if(v->header) { \
        c->ud_remote_credit = 0;                                \
        c->rcfp_remote_credit = 0;                              \
        c->rcrq_remote_credit = 0;                              \
    }\
} 
#endif
