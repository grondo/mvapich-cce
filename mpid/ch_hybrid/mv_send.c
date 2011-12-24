/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
 */

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

#include "mpid.h"
#include "mv_dev.h"
#include "mv_priv.h"
#include "mv_packet.h"
#include "queue.h"
#include "mv_param.h"
#include "dreg.h"
#include "mv_inline.h"

#define CHECK_CACHED_SEND(_c, _src_lrank, _tag, _context_id) \
    ((_c)->channel_rc_fp->cached_send.src_lrank == (_src_lrank) && \
     (_c)->channel_rc_fp->cached_send.tag == (_tag) && \
     (_c)->channel_rc_fp->cached_send.context_id == (_context_id))
#define COPY_CACHED_SEND(_c, _src_lrank, _tag, _context_id) { \
     (_c)->channel_rc_fp->cached_send.src_lrank = (_src_lrank); \
     (_c)->channel_rc_fp->cached_send.tag = (_tag); \
     (_c)->channel_rc_fp->cached_send.context_id = (_context_id); \
}

int MPID_MV_rendezvous_start(void *buf, int len, int src_lrank, int tag,
        int context_id, int dest_grank,
        MPIR_SHANDLE * shandle)
{
    int rc = MPI_SUCCESS;
    mvdev_connection_t *c = &mvdev.connections[dest_grank];
    mv_sbuf *v = get_sbuf(c, sizeof(mvdev_packet_rendezvous_start));

    void *buffer = v->header_ptr;
    mvdev_packet_rendezvous_start *packet = buffer;
    dreg_entry *dreg_entry = NULL;
    int proto = MVDEV_PROTOCOL_R3;

    /* fill in the packet */
    PACKET_SET_HEADER(packet, c, MVDEV_PACKET_RENDEZVOUS_START);
    PACKET_SET_ENVELOPE(packet, context_id, tag, len, src_lrank);

    shandle->transport = MVDEV_TRANSPORT_UD;

    if(c->xrc_enabled && len >= mvparams.rc_send_threshold) {
        dreg_entry = dreg_register(buf, len, DREG_ACL_READ);
        if(NULL != dreg_entry) {
            proto = MVDEV_PROTOCOL_REL_RPUT;
            if(mvparams.rc_rel)
                proto = MVDEV_PROTOCOL_UNREL_RPUT;
            shandle->transport = MVDEV_TRANSPORT_XRC;
        }
    }
    else if(c->rc_enabled && len >= mvparams.rc_send_threshold) {
        dreg_entry = dreg_register(buf, len, DREG_ACL_READ);
        if(NULL != dreg_entry) {
            proto = MVDEV_PROTOCOL_REL_RPUT;
            if(mvparams.rc_rel)
                proto = MVDEV_PROTOCOL_UNREL_RPUT;
            shandle->transport = MVDEV_TRANSPORT_RC;
        }
    }
    else if(mvparams.use_zcopy && len >= mvparams.zcopy_threshold && len <= (mvparams.mtu * 4096)) {
        dreg_entry = dreg_register(buf, len, DREG_ACL_READ);
        if(NULL != dreg_entry) {
            proto = MVDEV_PROTOCOL_UD_ZCOPY;
        }
    }

    if(len > mvparams.rc_setup_threshold) {
        c->total_messages++;
    }

    /* 
    if(!c->rc_enabled  && !c->channel_req
            && !c->rc_request
            && c->total_messages > mvparams.rc_threshold
            && mvdev.rc_connections < mvparams.max_rc_connections) {

        MV_Request_RC_Channel(c);
    }
    */

    packet->sreq = REQ_TO_ID(shandle);

    /* explicitly set protocol */
    shandle->protocol = packet->protocol = proto;

    shandle->local_address = buf;
    shandle->dreg_entry = dreg_entry;
    shandle->bytes_total = len;
    shandle->bytes_sent = 0;
    shandle->connection = c;
    shandle->is_complete = 0;
    shandle->blocked = 0;

    D_PRINT("Sending MVDEV_PACKET_RENDEZVOUS_START\n");

    mvdev_post_channel_send(c, v, sizeof(mvdev_packet_rendezvous_start));

    return rc;
}


int MPID_MV_self_start(void *buf, int len, int src_lrank, int tag,
                        int context_id, MPIR_SHANDLE * shandle)
{

    MPIR_RHANDLE *rhandle;
    int rc = MPI_SUCCESS;
    int found;

    shandle->dreg_entry = NULL;

    /* receive has been posted and is on the posted queue */
    MPID_Msg_arrived(src_lrank, tag, context_id, &rhandle, &found);

    if (found) {
        memcpy(rhandle->buf, buf, len);
        rhandle->s.MPI_TAG = tag;
        rhandle->s.MPI_SOURCE = src_lrank;
        RECV_COMPLETE(rhandle);

        rhandle->s.count = len;
        SEND_COMPLETE(shandle);
        /* need to call the finish routine for the receive? */
        return rc;
    }

    /* receive handle has been created
     * and posted on unexpected queue */
    rhandle->connection = NULL;

    if (len < mvparams.rendezvous_threshold) {
        /* copy the buffer and mark the send complete so there is a bit of
         * buffering for silly programs that send-to-self blocking.  Hide
         * info on the send in vbuf_{head,tail} which we know will not
         * be used.  */
        rhandle->send_id = 0;
        rhandle->s.count = len;
        rhandle->vbuf_head = malloc(len);
        rhandle->vbuf_tail = (void *) ((aint_t) len);
        memcpy(rhandle->vbuf_head, buf, len);
        SEND_COMPLETE(shandle);
    } else {
        rhandle->send_id = REQ_TO_ID(shandle);
        rhandle->s.count = len;
        shandle->local_address = buf;
        shandle->is_complete = 0;
        shandle->bytes_total = len;
    }

    return rc;
}

void MPID_MV_self_finish(MPIR_RHANDLE * rhandle,
                          MPIR_RHANDLE * unexpected)
{
    MPIR_SHANDLE *shandle;

    void *send_ptr;
    int bytes_total;
    int truncate = 0;
    shandle = (MPIR_SHANDLE *) ID_TO_REQ(unexpected->send_id);

    if (shandle) {
        send_ptr = shandle->local_address;
        bytes_total = shandle->bytes_total;
    } else {
        send_ptr = unexpected->vbuf_head;
        bytes_total = (aint_t) unexpected->vbuf_tail;
    }

    if (bytes_total > rhandle->len) {
        /* user error */
        truncate = 1;
    } else {
        /* fewer or same bytes were sent as were specified in the recv */
        rhandle->len = bytes_total;
    }

    memcpy(rhandle->buf, send_ptr, rhandle->len);

    /* this is similate to copy_unexpected_handle_to_user_handle, but
     * we need many fewer fields
     */
    rhandle->s.MPI_TAG = unexpected->s.MPI_TAG;
    rhandle->s.MPI_SOURCE = unexpected->s.MPI_SOURCE;
    rhandle->connection = NULL;
    RECV_COMPLETE(rhandle);

    if (truncate)
        rhandle->s.MPI_ERROR = MPI_ERR_TRUNCATE;

    if (shandle) {
        SEND_COMPLETE(shandle);
    } else {
        free(unexpected->vbuf_head);
    }
}


int MPID_MV_eager_send(void *buf, int len, int src_lrank, int tag,
        int context_id, int dest_grank, MPIR_SHANDLE * s)
{
    mvdev_connection_t *c = &mvdev.connections[dest_grank];
    mv_sbuf *v;
    int bytes_copied = 0, tmp_bytes = 0;

    s->dreg_entry = NULL;

    D_PRINT("Sending from mvdev_send_eager_ud\n");
    D_PRINT("Sending to rank %d, connrank: %d\n", dest_grank, c->global_rank);

    s->is_complete = 0;
    s->protocol = MVDEV_PROTOCOL_EAGER;

    v = get_sbuf(c, len + sizeof(mvdev_packet_eager_start));

    if(v->transport == MVDEV_TRANSPORT_RCFP && len <= 128 && mvparams.use_header_caching) {
        if(MVDEV_LIKELY(CHECK_CACHED_SEND(c, src_lrank, tag, context_id))) {
            if(len) memcpy(v->header_ptr, buf, len);

#ifdef XRC
            if(c->xrc_enabled) {
                MV_Send_RCFP_Cached(c, v, len, &(c->xrc_channel.shared_channel->qp[0]));
            } else 
#endif
            {
                MV_Send_RCFP_Cached(c, v, len, &(c->rc_channel_head->qp[0]));
            }
            v->shandle = NULL;
            SEND_COMPLETE(s);
            return 0;
        } else {
            COPY_CACHED_SEND(c, src_lrank, tag, context_id);
        }
    }

    {
        mvdev_packet_eager_start *header;
        mvdev_packet_eager_next *header2;
        void *databuf;

        header = (mvdev_packet_eager_start *) v->header_ptr;
        v->rank = dest_grank;

        PACKET_SET_HEADER(header, c, MVDEV_PACKET_EAGER_START);
        PACKET_SET_ENVELOPE(header, context_id, tag, len, src_lrank);

        databuf = ((char *) header) + sizeof(mvdev_packet_eager_start);

        if(MVDEV_LIKELY(len)) {
            tmp_bytes = MIN(SBUF_MAX_DATA_SZ_HDR(v, mvdev_packet_eager_start), len);
            memcpy(databuf, buf, tmp_bytes);
            bytes_copied = tmp_bytes;
        }

        mvdev_post_channel_send(c, v, bytes_copied + sizeof(mvdev_packet_eager_start));

        while(MVDEV_UNLIKELY(bytes_copied < len)) {
            v = get_sbuf(c, len + sizeof(mvdev_packet_eager_next));
            header2 = (mvdev_packet_eager_next *) v->header_ptr;
            PACKET_SET_HEADER(header2, c, MVDEV_PACKET_EAGER_NEXT);
            databuf = ((char *) header2) + sizeof(mvdev_packet_eager_next);

            tmp_bytes = MIN(SBUF_MAX_DATA_SZ_HDR(v, mvdev_packet_eager_next), len - bytes_copied);
            memcpy(databuf, ((char *)buf) + bytes_copied, tmp_bytes);

            mvdev_post_channel_send(c, v, tmp_bytes + sizeof(mvdev_packet_eager_next));
            bytes_copied += tmp_bytes;
        }
    }

    v->shandle = NULL;
    MV_ASSERT(len == bytes_copied);
    SEND_COMPLETE(s);

    return 0;
}

