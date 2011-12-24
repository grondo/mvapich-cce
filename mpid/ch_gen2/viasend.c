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
#include "nfr.h"
#include "viadev.h"
#include "viapriv.h"
#include "dreg.h"
#include "viapacket.h"
#include "queue.h"
#include "viaparam.h"

#ifdef MCST_SUPPORT
#include "bcast_info.h"
extern bcast_info_t *bcast_info;
int retransmission = 0;
int HEADER =  sizeof(viadev_packet_eager_start);
void process_acks(void);
void slide_window(void);
void check_time_out(void);
int MPID_VIA_mcst_send(void *, int, MPIR_SHANDLE *);
void vbuf_init_msend(vbuf *, unsigned long);
void MPID_RC_Send(void *, int, int, int, int, int, int *);
#endif

int MPID_VIA_rendezvous_start(void *buf, int len, int src_lrank, int tag,
                              int context_id, int dest_grank,
                              MPIR_SHANDLE * shandle)
{

    /*
     * Construct a packet to start a rendezvous transfer. 
     * If dreg_entry is NULL, memory is not pinned, otherwise
     * it is. 
     */
    dreg_entry *dreg_entry;

    /* We can potentially use ADAPTIVE_RDMA_FAST_PATH 
     * here 
     */

    int rc = MPI_SUCCESS;

    vbuf *v = get_vbuf();
    void *buffer = VBUF_BUFFER_START(v);
    viadev_packet_rendezvous_start *packet = buffer;
    viadev_connection_t *c = &viadev.connections[dest_grank];
    int proto = viadev_rndv_protocol;

    if(!viadev_use_dreg_cache && len < viadev_r3_nocache_threshold) {
        proto = VIADEV_PROTOCOL_R3;
    }

#ifdef MEMORY_RELIABLE
    proto = VIADEV_PROTOCOL_R3;
#endif

    /* if this fails, will return NULL, which is what we want. 
     * So no need to do anything other than try to register. 
     */
    dreg_entry = NULL;

    if (0 == len || len < viadev_r3_threshold) {
        proto = VIADEV_PROTOCOL_R3;
        D_PRINT("rendzvous enter: set R3 since len=0 or less than viadev_r3_threshold (proto=%d)\n", proto);
    } 
    else if ((buf != NULL) && (len > 0) && proto != VIADEV_PROTOCOL_R3) {
        dreg_entry = dreg_register(buf, len, DREG_ACL_READ);
        if (NULL == dreg_entry)
            proto = VIADEV_PROTOCOL_R3;
    }

    /* fill in the packet */
    PACKET_SET_HEADER_NFR(packet, c, VIADEV_PACKET_RENDEZVOUS_START);
    PACKET_SET_ENVELOPE(packet, context_id, tag, len, src_lrank);
    packet->sreq = REQ_TO_ID(shandle);

    /* may be less than receive buffer size */
    packet->len = len;
    packet->buffer_address = buf;

    if (NULL != dreg_entry) {
        packet->memhandle_rkey = dreg_entry->memhandle->rkey;
    }

    /* explicitly set protocol */
    shandle->protocol = proto;
    packet->protocol = proto;

    /* prepare descriptor and post */
    vbuf_init_send(v, sizeof(viadev_packet_rendezvous_start));

    viadev_post_send(c, v);

    /* we can post the vbuf before updating the send handle. 
     * If we go to a multithreaded implementation, in which 
     * another thread may process the completed vbuf, we will
     * need to update the shandle before posting the send
     * to avoid a race condition
     */

    shandle->local_address = buf;
    shandle->dreg_entry = dreg_entry;
    shandle->bytes_total = len;
    shandle->bytes_sent = 0;
    shandle->connection = c;
    shandle->is_complete = 0;
    shandle->rndv_start = v;

    /* fill in when we get the reply:
     * receive_id, remote_address, memory_handle
     */

    return rc;
}


int MPID_VIA_self_start(void *buf, int len, int src_lrank, int tag,
                        int context_id, MPIR_SHANDLE * shandle)
{

    MPIR_RHANDLE *rhandle;
    int rc = MPI_SUCCESS;
    int found;

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

    if (len < viadev_rendezvous_threshold) {
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

void MPID_VIA_self_finish(MPIR_RHANDLE * rhandle,
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

    if (bytes_total > rhandle->len)
        /* user error */
        truncate = 1;
    else
        /* fewer or same bytes were sent as were specified in the recv */
        rhandle->len = bytes_total;

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


int viadev_eager_ok(int len, viadev_connection_t * c)
{
    if(VIADEV_LIKELY(viadev_use_srq)) {
        if(VIADEV_LIKELY(c->ext_sendq_size <= viadev_eager_ok_threshold)) {   
            return 1;
        } 
        else {
            return 0;
        }
    } else {
        int nvbufs;
        nvbufs = viadev_calculate_vbufs_expected(len, VIADEV_PROTOCOL_EAGER);

        if (c->remote_credit - nvbufs >= viadev_credit_preserve) {
            return 1;
        }

        return 0;
    }
}

int MPID_VIA_eager_send(void *buf, int len, int src_lrank, int tag,
                        int context_id, int dest_grank, MPIR_SHANDLE * s)
{

    int startmax = VBUF_DATA_SIZE(viadev_packet_eager_start);
    int nextmax = VBUF_DATA_SIZE(viadev_packet_eager_next);
    viadev_packet_eager_start *h;
    viadev_packet_eager_next *d;
    void *databuf;
    viadev_connection_t *c = &viadev.connections[dest_grank];
    vbuf *v;

#ifdef ADAPTIVE_RDMA_FAST_PATH
    int rdma_ok;
#endif

    int bytes_sent;

    D_PRINT("eager send \n");

    /*
     * we could fill in the send handle, but I think there is no
     * need, since we are sending all the data right here and now. 
     * However, is_complete should be set to zero because
     * the MPI send does not actually complete until the
     * VIA send completes. 
     */

    s->is_complete = 0;
    s->protocol = VIADEV_PROTOCOL_EAGER;


    /* Set dreg_entry to NULL because SEND_COMPLETE will try to decrement
     * the reference count if it is non-null
     */
    s->dreg_entry = NULL;

    if(eager_coalesce_ok(c, len)) {
        viadev_packet_envelope envelope;

        envelope.context     = context_id;
        envelope.tag         = tag;
        envelope.src_lrank   = src_lrank;
        envelope.data_length = len;

        eager_coalesce(c, buf, len, &envelope);

        SEND_COMPLETE(s);
        return 0;
    }

#ifdef ADAPTIVE_RDMA_FAST_PATH
    rdma_ok = fast_rdma_ok(c, 
            sizeof(viadev_packet_eager_start) + len, 1);

#if (!defined(DISABLE_HEADER_CACHING) && \
        defined(ADAPTIVE_RDMA_FAST_PATH))
    if (rdma_ok && (len < viadev_max_fast_eager_size)) {

        v = &(c->RDMA_send_buf[c->phead_RDMA_send]);
        h = (viadev_packet_eager_start *) v->buffer;
        /* set up the packet */
        if(!viadev_use_nfr) {
            PACKET_SET_ENVELOPE((&viadev.match_hdr), context_id, tag, len, src_lrank);
            PACKET_SET_CREDITS(((viadev_packet_header *) &viadev.match_hdr.header), c);
        } else {
            PACKET_SET_ENVELOPE((&v->match_hdr), context_id, tag, len, src_lrank);
            PACKET_SET_CREDITS(((viadev_packet_header *) &v->match_hdr.header), c);
            NFR_SET_ACK(((viadev_packet_header *) &v->match_hdr.header), c);
        }

#ifdef EARLY_SEND_COMPLETION
        v->shandle = NULL;
#else
        /* set the handle */
        v->shandle = s;
#endif

        if (search_header_cache(c, 
                    (viadev_use_nfr ?
                    (viadev_packet_eager_start *)&v->match_hdr:
                    (viadev_packet_eager_start *)&viadev.match_hdr))) {
            PACKET_SET_HEADER_OPT(h, c, FAST_EAGER_CACHED);
            h->header.fast_eager_size = (unsigned char) len;
            databuf = ((char *) h) + FAST_EAGER_HEADER_SIZE;
            memcpy(databuf, buf, len);
            /* figure out how much data to send in this packet */
            post_fast_rdma_with_completion(c,
                                           FAST_EAGER_HEADER_SIZE + len);

        } else {
            PACKET_SET_ENVELOPE(h, context_id, tag, len, src_lrank);
            PACKET_SET_HEADER_OPT(h, c, VIADEV_PACKET_EAGER_START);

            /* Set some of the credit fields */
            if(!viadev_use_nfr) {
                h->header.vbuf_credit = viadev.match_hdr.header.vbuf_credit;
                h->header.rdma_credit = viadev.match_hdr.header.rdma_credit;
                h->header.remote_credit = viadev.match_hdr.header.remote_credit;
            } else {
                h->header.vbuf_credit = v->match_hdr.header.vbuf_credit;
                h->header.rdma_credit = v->match_hdr.header.rdma_credit;
                h->header.remote_credit = v->match_hdr.header.remote_credit;
            }
            if (viadev_use_nfr) {
                h->header.ack = v->match_hdr.header.ack;
            }
            
            databuf = ((char *) h) + sizeof(viadev_packet_eager_start);
            memcpy(databuf, buf, len);
            h->bytes_in_this_packet = len;
            /* figure out how much data to send in this packet */
            post_fast_rdma_with_completion(c,
                                           sizeof
                                           (viadev_packet_eager_start) +
                                           len);
        }

#ifdef EARLY_SEND_COMPLETION
        /* mark the send as completed */
        SEND_COMPLETE(s);
#endif
        return 0;

    }
#endif                          /* of HEADER_CACHING */

    if (rdma_ok) {

        v = &(c->RDMA_send_buf[c->phead_RDMA_send]);
        h = (viadev_packet_eager_start *) v->buffer;
        databuf = ((char *) h) + sizeof(viadev_packet_eager_start);

        /* figure out how much data to send in this packet */
        h->bytes_in_this_packet = len;

        if (startmax < h->bytes_in_this_packet) {
            h->bytes_in_this_packet = startmax;
        }

        /* copy next chunk of data in to vbuf */
        if (h->bytes_in_this_packet != 0) {
            memcpy(databuf, buf, h->bytes_in_this_packet);
        }

        bytes_sent = h->bytes_in_this_packet;

        /* set up the packet */
        PACKET_SET_HEADER_NFR(h, c, VIADEV_PACKET_EAGER_START);
        PACKET_SET_ENVELOPE(h, context_id, tag, len, src_lrank);

#ifdef EARLY_SEND_COMPLETION
        v->shandle = NULL;
#else
        /*
         * This is a signal to the VIA send completion handler
         * that the MPI send should be marked complete.
         */
        if (bytes_sent == len) {
            v->shandle = s;
        } else {
            v->shandle = NULL;
        }
#endif
        post_fast_rdma_with_completion(c,
                                       sizeof(viadev_packet_eager_start) +
                                       h->bytes_in_this_packet);
        /* Multiple packet eager send over RDMA */
        while (bytes_sent < len) {

            /* Get the next vbuf */
            v = &(c->RDMA_send_buf[c->phead_RDMA_send]);

            /* figure out where header and data are */
            d = (viadev_packet_eager_next *) VBUF_BUFFER_START(v);
            databuf = ((char *) d) + sizeof(viadev_packet_eager_next);

            /* set up the packet */
            PACKET_SET_HEADER_NFR(d, c, VIADEV_PACKET_EAGER_NEXT);

            /* figure out how much data to send in this packet */
            d->bytes_in_this_packet = len - bytes_sent;
            if (nextmax < d->bytes_in_this_packet) {
                d->bytes_in_this_packet = nextmax;
            }

            /* copy next chunk of data in to vbuf */
            memcpy(databuf, ((char *) buf) + bytes_sent,
                    d->bytes_in_this_packet);

            bytes_sent += d->bytes_in_this_packet;

            /* finally send it off */
#ifdef EARLY_SEND_COMPLETION
            v->shandle = NULL;
#else
            /*
             * This is a signal to the VIA send completion handler
             * that the MPI send should be marked complete.
             */
            if (bytes_sent == len) {
                v->shandle = s;
            } else {
                v->shandle = NULL;
            }
#endif

            /* figure out how much data to send in this packet */
            post_fast_rdma_with_completion(c,
                    sizeof(viadev_packet_eager_next) +
                    d->bytes_in_this_packet);
        }

#ifdef EARLY_SEND_COMPLETION
        /* mark the send as completed */
        SEND_COMPLETE(s);
#endif

        return 0;
    }
#endif                          /* RDMA_FAST_PATH */

    /* If control reaches here, we have fallen back on 
     * send/recv channel for eager messages */

    v = get_vbuf();
    h = (viadev_packet_eager_start *) VBUF_BUFFER_START(v);
    databuf = ((char *) h) + sizeof(viadev_packet_eager_start);

    /* set up the packet */
    PACKET_SET_HEADER_NFR(h, c, VIADEV_PACKET_EAGER_START);
    PACKET_SET_ENVELOPE(h, context_id, tag, len, src_lrank);

    /* figure out how much data to send in this packet */
    h->bytes_in_this_packet = len;

    if (startmax < h->bytes_in_this_packet) {
        h->bytes_in_this_packet = startmax;
    }

    /* copy next chunk of data in to vbuf */

    if (h->bytes_in_this_packet != 0) {
        memcpy(databuf, buf, h->bytes_in_this_packet);
    }

    bytes_sent = h->bytes_in_this_packet;

    /* finally send it off */
    vbuf_init_send(v, (sizeof(viadev_packet_eager_start) +
                       h->bytes_in_this_packet));

#ifdef EARLY_SEND_COMPLETION
    v->shandle = NULL;
#else
    /*
     * This is a signal to the VIA send completion handler
     * that the MPI send should be marked complete.
     */
    if (bytes_sent == len) {
        v->shandle = s;
    } else {
        v->shandle = NULL;
    }
#endif

    viadev_post_send(c, v);

    while (bytes_sent < len) {

        v = get_vbuf();

        /* figure out where header and data are */
        d = (viadev_packet_eager_next *) VBUF_BUFFER_START(v);
        databuf = ((char *) d) + sizeof(viadev_packet_eager_next);

        /* set up the packet */
        PACKET_SET_HEADER_NFR(d, c, VIADEV_PACKET_EAGER_NEXT);

        /* figure out how much data to send in this packet */
        d->bytes_in_this_packet = len - bytes_sent;
        if (nextmax < d->bytes_in_this_packet) {
            d->bytes_in_this_packet = nextmax;
        }

        /* copy next chunk of data in to vbuf */
        memcpy(databuf, ((char *) buf) + bytes_sent,
               d->bytes_in_this_packet);

        bytes_sent += d->bytes_in_this_packet;

        /* finally send it off */
        vbuf_init_send(v, (sizeof(viadev_packet_eager_next) +
                           d->bytes_in_this_packet));

#ifdef EARLY_SEND_COMPLETION
        v->shandle = NULL;
#else
        /*
         * This is a signal to the VIA send completion handler
         * that the MPI send should be marked complete.
         */
        if (bytes_sent == len) {
            v->shandle = s;
        } else {
            v->shandle = NULL;
        }
#endif
        viadev_post_send(c, v);
    }

    if(!viadev_use_srq) {

        /* NOTE: even with the backlog queue, this assertion should
         * be true in a single threaded implementation.
         * The backlog queue only has something on it if remote_credit == 0.
         * We did a eager send because there were enough remote credits
         * to handle the transfer and these should have been the only
         * sends we posted since that check.
         */

        assert(c->remote_credit >= viadev_credit_preserve);
    }

#ifdef EARLY_SEND_COMPLETION
    /* mark the send as completed */
    SEND_COMPLETE(s);
#endif

    return 0;
}


void viadev_rput(viadev_connection_t * c, vbuf * v, void *local_address,
                 uint32_t local_memhandle, void *remote_address,
                 uint32_t remote_memhandle, int nbytes)
{
    void *buffer = VBUF_BUFFER_START(v);
    viadev_packet_rput *h = (viadev_packet_rput *)buffer;
    D_PRINT("viadev_rput: RDMA write\n");
    vbuf_init_rput(v, local_address, local_memhandle,
                   remote_address, remote_memhandle, nbytes);
    h->header.type = VIADEV_PACKET_RPUT;
    viadev_post_rdmawrite(c, v);
}

#ifdef MEMORY_RELIABLE
int MPID_VIA_ack_send(int dest_grank, packet_sequence_t ack, int ack_type)
{

    int rc = MPI_SUCCESS;

    vbuf *v = get_vbuf();
    void *buffer = VBUF_BUFFER_START(v);
    viadev_packet_crc_ack *packet = buffer;
    viadev_connection_t *c = &viadev.connections[dest_grank];

    
    /* fill in the packet */
    PACKET_SET_HEADER(packet, c, VIADEV_PACKET_CRC_ACK);
    packet->acked_seq_number = ack;
    packet->ack_type = ack_type;


    /* prepare descriptor and post */
    vbuf_init_send(v, sizeof(viadev_packet_crc_ack));
    viadev_post_send(c, v);

    return rc;
}
#endif


#ifdef MCST_SUPPORT
/* Collecting the min. ack for a coroot */

void process_acks(void)
{

    int i, j;
    int *ack_ptr;
    int *ack_buf;
    int min_ack = -1;
    int firsttime = 1;
    int num_coroots ;
    int num_child ;

    ack_ptr = (int *) bcast_info->ack_buffer;

    if (LEFT_OVERS == 0) {
        num_coroots = NUM_COROOTS;
        num_child = NUM_CHILD;
    }
    else{
        if (viadev.me == 0){
            num_coroots = NUM_COROOTS;
            num_child = NUM_CHILD + LEFT_OVERS;
        }
        else {
            if (viadev.me > LAST_NODE){
                num_coroots = 1;
                num_child = viadev.np;
            }
            else{
                num_coroots = NUM_COROOTS;
                num_child = NUM_CHILD;
            }
        }
    }

    for (i = 0; i < num_coroots; i++) {
        ack_buf = (int *) (&ack_ptr[num_child * i]);

        for (j = 0; j < num_child; j++) {
            if (j == viadev.me % num_child)
                continue;
            if (firsttime)
                min_ack = ack_buf[j];
            else if (ack_buf[j] < min_ack)
                min_ack = ack_buf[j];
            firsttime = 0;
        }
        firsttime = 1;
        bcast_info->min_ack[i] = min_ack;
        co_print("co_index:%d,min_ack:%d\n", i, min_ack);
    }

}

void slide_window(void)
{

    int i, k, acks_recvd;
    int win_head, win_tail, buf_head, buf_tail, min_ack;
    double *time_out;
    int num_coroots;

    if (LEFT_OVERS == 0){
        num_coroots = NUM_COROOTS;
    }
    else{
        if (viadev.me > LAST_NODE)
            num_coroots = 1;
        else num_coroots = NUM_COROOTS;
    }

    for (i = 0; i < num_coroots; i++) {
        win_head = bcast_info->win_head[i];
        win_tail = bcast_info->win_tail[i];
        buf_head = bcast_info->buf_head[i];
        buf_tail = bcast_info->buf_tail[i];
        min_ack = bcast_info->min_ack[i];
        time_out = bcast_info->time_out[i];


        if (min_ack == -1)
            continue;

        if ((min_ack >= win_tail) && (min_ack < win_head)) {

            acks_recvd = min_ack - win_tail + 1;
            if (acks_recvd > 0)
                bcast_info->is_full[i] = 0;

            /* Resetting all the time-outs of received messages to -1 */
            for (k = win_tail; k <= min_ack; k++) {
                time_out[buf_tail] = -1.0;
                buf_tail = buf_tail + 1;
                buf_tail = buf_tail % SENDER_WINDOW;
            }

            bcast_info->win_tail[i] = min_ack + 1;
            bcast_info->buf_tail[i] = buf_tail;

        } else {
            /*Check if the ack is within the window..
             * Check if the buffer pointers are moved correctly.
             */
            if (min_ack > win_head) {
                bcast_info->win_head[i] = bcast_info->win_tail[i] =
                    min_ack + 1;
                bcast_info->buf_tail[i] = bcast_info->buf_head[i];
                bcast_info->is_full[i] = 0;
            }
        }
    }

}

void check_time_out(void)
{

    int i, l, Dest, buf_tail, win_tail;
    int buf_head, win_head, sentinal;
    vbuf *v;
    viadev_packet_eager_start *h;
    void *databuf;
    int error_code;
    double *time_out_array;
    double time_out;
    int *recent_ack;
    int *ack_ptr;
    int num_coroots;
    int num_child;

    if (LEFT_OVERS == 0){
        num_coroots = NUM_COROOTS;
        num_child = NUM_CHILD;
    }
    else{
        if (viadev.me > LAST_NODE){
            num_coroots = 1;num_child = viadev.np;
        }
        else {
            if (viadev.me == 0){
                num_coroots = NUM_COROOTS;
                num_child = NUM_CHILD + LEFT_OVERS;
            }
            else {
                num_coroots = NUM_COROOTS;num_child = NUM_CHILD;
            }
        }
    }

    ack_ptr = (int *) bcast_info->ack_buffer;

    for (i = 0; i < num_coroots; i++) {

        recent_ack = (int *) (&ack_ptr[num_child * i]);
        buf_tail = bcast_info->buf_tail[i];
        win_tail = bcast_info->win_tail[i];
        win_head = bcast_info->win_head[i];
        buf_head = bcast_info->buf_head[i];
        time_out_array = bcast_info->time_out[i];


        sentinal = buf_head;
        co_print("buf_tail:%d, buf_head:%d,is_full[%d]:%d\n",
                 buf_tail, buf_head, i, bcast_info->is_full[i]);

        if ((buf_tail == buf_head) && (bcast_info->is_full[i] == 0))
            continue;
        else if ((buf_tail == buf_head) && (bcast_info->is_full[i] == 1))
            sentinal = (buf_tail + SENDER_WINDOW - 1) % SENDER_WINDOW;

        while (buf_tail != sentinal) {
            time_out = time_out_array[buf_tail];

            if ((get_us() < time_out) || (time_out == -1)) {
                break;
            } else {            /* Retransmit the timed_out message */

                v = &(bcast_info->co_buf[i][buf_tail]);
                h = (viadev_packet_eager_start *) (v);
                databuf = ((char *) h) + sizeof(viadev_packet_eager_start);

                for (l = 0; l < num_child; l++) {
                    if (l == viadev.me % num_child)
                        continue;
                    if (recent_ack[l] < win_tail) {
                        if (LEFT_OVERS == 0 )
                        Dest = CHILD_TO_RANK(l);
                        else {
                            if (viadev.me == 0){
                                if (l < NUM_CHILD) Dest = l;
                                else Dest = LAST_NODE + (l + 1  - NUM_CHILD);
                            }
                            else {
                                if (viadev.me > LAST_NODE){
                                    Dest = l;
                                }
                                else{
                                    Dest = CHILD_TO_RANK(l);
                                }
                            }
                        }
                        co_print("Retransmission\n");
                        co_print
                            ("Dest:%d, tag:%d, src_lrank:%d, data_length:%d"
                             "\n", Dest, (h->envelope).tag,
                             (h->envelope).src_lrank,
                             (h->envelope).data_length);

                        retransmission = 1;
                        MPID_RC_Send(databuf, (h->envelope).data_length,
                                     (h->envelope).src_lrank,
                                     (h->envelope).tag,
                                     -1, Dest, &error_code);
                        retransmission = 0;
                    }
                }
                time_out_array[buf_tail] = -1.0;
                ++bcast_info->win_tail[i];
                bcast_info->buf_tail[i] = (++bcast_info->buf_tail[i]) %
                    SENDER_WINDOW;
                bcast_info->is_full[i] = 0;
                buf_tail = buf_tail + 1;
                buf_tail = buf_tail % SENDER_WINDOW;
                ++win_tail;
                time_out_array[buf_tail] = get_us() + 1000;
            }

        }

    }

}


int MPID_VIA_mcst_send(void *buf, int len, MPIR_SHANDLE * s)
{

    vbuf *v;
    viadev_packet_eager_start *h;

    int tag, context_id = -1;
    int src_lrank = viadev.me;
    int bytes_sent;
    int result, buf_head;
    int co_rank, dst, root, Dest;
#ifndef DIRECT_COROOT
    int mask, relative_rank, size;
#endif
    static int Bcnt = 0;
    int error_code;
    int startmax = VBUF_DATA_SIZE(viadev_packet_eager_start);
    void *databuf;
    struct ibv_send_wr *bad_wr;


    viadev.bcast_info.bcast_called = 1;

    if ((LEFT_OVERS == 0) || (viadev.me <= LAST_NODE))
        co_rank = CORANK(viadev.me);
    else
        co_rank = 0;

    s->is_complete = 0;

    if (viadev.bcast_info.is_full[co_rank]) {
        /*  Spin till this condition becomes void. 
         */
        co_print("Buffer full at the root\n");
        while (viadev.bcast_info.is_full[co_rank] == 1) {
            process_acks();
            slide_window();
            check_time_out();
        }

    }

    v = get_vbuf();

    /* Update the window parameters */
    tag = bcast_info->win_head[co_rank];
    co_print("Entering bcast no:%d", tag);

    h = (viadev_packet_eager_start *) VBUF_BUFFER_START(v);
    databuf = ((char *) h) + sizeof(viadev_packet_eager_start);

    /* set up the packet */
    PACKET_SET_ENVELOPE(h, context_id, tag, len, src_lrank);
    (h->header).type = VIADEV_PACKET_UD_MCST;
    (h->envelope).data_length = len;
    h->bytes_in_this_packet = len;
    if (startmax < h->bytes_in_this_packet)
        h->bytes_in_this_packet = startmax;

    /* copy data into vbuf */
    if (h->bytes_in_this_packet != 0) {
        memcpy(databuf, buf, h->bytes_in_this_packet);
    }
    bytes_sent = h->bytes_in_this_packet;


    /* finally send it off */
    vbuf_init_msend(v, sizeof(viadev_packet_eager_start) +
                    h->bytes_in_this_packet);
    if (ibv_post_send (viadev.ud_qp_hndl, &(v->desc.u.sr), &bad_wr))
            printf("post failed\n");

    if ((LEFT_OVERS == 0) || (viadev.me <= LAST_NODE)){
    /* Broadcast  to the co-roots */
#ifdef DIRECT_COROOT

    Bcnt = tag;
    root = viadev.me;
    for (dst = 0; dst < NUM_COROOTS; dst++) {
        if (dst == CORANK(viadev.me))
            continue;
        Dest = CORANK_TO_RANK(dst);
        retransmission = 1;
        MPID_RC_Send(buf, len, root, Bcnt, -1, Dest, &error_code);
        retransmission = 0;
    }

#else
    mask = 0x1;
    root = viadev.me;
    size = NUM_COROOTS;
    relative_rank =
        (co_rank >=
         root / NUM_COROOTS) ? co_rank - (root / NUM_COROOTS) : co_rank -
        (root / NUM_COROOTS) + size;
    while (mask < size)
        mask <<= 1;
    mask >>= 1;

    while (mask > 0) {
        if (mask < size) {
            dst = co_rank + mask;
            if (dst >= size)
                dst -= size;
            Dest = CORANK_TO_RANK(dst);
            co_print("Dest:%d\n", Dest);
            Bcnt = tag;
            retransmission = 1;
            MPID_RC_Send(buf, len, root, Bcnt, -1, Dest, &error_code);
            retransmission = 0;
        }
        mask >>= 1;
    }

#endif
    }

    /* Finally copy the message into the window */
    bcast_info->win_head[co_rank] = ++(bcast_info->win_head[co_rank]);

    buf_head = bcast_info->buf_head[co_rank];
    bcast_info->buf_head[co_rank] =
        (++bcast_info->buf_head[co_rank]) % SENDER_WINDOW;

    if (bcast_info->buf_head[co_rank] == bcast_info->buf_tail[co_rank])
        viadev.bcast_info.is_full[co_rank] = 1;
    else
        viadev.bcast_info.is_full[co_rank] = 0;

    bcast_info->time_out[co_rank][buf_head] = get_us() + BCAST_TIME_OUT;

    v = &(bcast_info->co_buf[co_rank][buf_head]);


    memcpy(v, h,
           sizeof(viadev_packet_eager_start) + h->bytes_in_this_packet);
    h = (viadev_packet_eager_start *) v;
    co_print
        ("After copying,tag:%d,src:%d,length:%d,buf_head:%d,address:%p\n",
         (h->envelope).tag, (h->envelope).src_lrank,
         (h->envelope).data_length, buf_head, v);

    /* Process the acks here */
    process_acks();

    /* Slide the window here */
    slide_window();

    /* Check the time out here */
    check_time_out();


    return 1;

}
#endif
