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


#include <stdio.h>
#include <unistd.h>
#include "mpid.h"
#include "ibverbs_header.h"
#include "viadev.h"
#include "viapriv.h"
#include "viutil.h"
#include "vbuf.h"
#include "viapacket.h"
#include "nfr.h"

inline int match_cached_out_envelope(viadev_connection_t * c, 
        viadev_packet_envelope * e) {
    return (c->coalesce_cached_out.context == e->context 
            && c->coalesce_cached_out.tag == e->tag
            && c->coalesce_cached_out.data_length == e->data_length
            && c->coalesce_cached_out.src_lrank == e->src_lrank) ? 1 : 0;
}

#define COPY_ENVELOPE(_dest, _from, _c) \
{ \
    (_c)->coalesce_cached_out.context     = (_dest)->context     = (_from)->context; \
    (_c)->coalesce_cached_out.tag         = (_dest)->tag         = (_from)->tag; \
    (_c)->coalesce_cached_out.data_length = (_dest)->data_length = (_from)->data_length; \
    (_c)->coalesce_cached_out.src_lrank   = (_dest)->src_lrank   = (_from)->src_lrank; \
}

static inline vbuf * get_eager_coalesce_vbuf(viadev_connection_t * c, 
        int len, viadev_packet_envelope * envelope) {
    vbuf * tail_v; 
    vbuf * to_use = NULL; 
    int total_len = 
        len + sizeof(viadev_packet_eager_coalesce_full) +
        VBUF_FAST_RDMA_EXTRA_BYTES;

    /* if we already have an entry, does it have room? */
    if(c->ext_sendq_head) {
        tail_v = c->ext_sendq_tail; 

        /* is it of the proper type? */
        if(tail_v->len != 0 && tail_v->bytes_remaining >= total_len &&
                (!viadev_use_eager_coalesce_limited || 
                 match_cached_out_envelope(c, envelope))) {
#ifdef DEBUG
            viadev_packet_header * tail_h = 
                (viadev_packet_header *) tail_v->buffer; 
            assert(tail_h->type == VIADEV_PACKET_EAGER_COALESCE);
#endif
            return tail_v;
        } else {
            /* there are items in the sendq that we cannot
             * coalesce to anymore -- so just send it out
             */
            viadev_ext_sendq_send(c);
        }
    } 

    D_PRINT("No luck with looking for existing ones\n");

    /* if we get here we need to start a new packet */
    {
        viadev_packet_eager_coalesce * h;
        total_len += sizeof(viadev_packet_eager_coalesce);

        /* try to get an FP one */
#ifdef ADAPTIVE_RDMA_FAST_PATH
        {
            int rdma_ok;
            rdma_ok = fast_rdma_ok(c, total_len, 1);
            if(rdma_ok) {
                to_use = &(c->RDMA_send_buf[c->phead_RDMA_send]); 

                c->RDMA_send_buf[c->phead_RDMA_send].padding = BUSY_FLAG;
                if (++(c->phead_RDMA_send) >= viadev_num_rdma_buffer)
                    c->phead_RDMA_send = 0;

                h = (viadev_packet_eager_coalesce *) to_use->buffer;
                to_use->bytes_remaining = VBUF_BUFFER_SIZE - 
                    VBUF_FAST_RDMA_EXTRA_BYTES;
            } 
         }
#endif

        if(NULL == to_use) {
            D_PRINT("Getting a vbuf for send/recv\n");
            /* take one for send/recv */
            to_use = get_vbuf();
            to_use->bytes_remaining = VBUF_BUFFER_SIZE;
            h = (viadev_packet_eager_coalesce *) VBUF_BUFFER_START(to_use);
            D_PRINT("buf: %p, h: %p\n", to_use->buffer, h);
            if(!viadev_use_srq) {
                c->remote_credit--;
            }
        }

        /* few bookkeeping items are needed to start a coalesced
         * packet
         */
        h->pkt_count = 0;
        h->header.id = c->next_packet_tosend++;
        h->header.type = VIADEV_PACKET_EAGER_COALESCE;
        h->header.src_rank = viadev.me;

        h->header.vbuf_credit = 0;
        h->header.remote_credit = 0;
#ifdef ADAPTIVE_RDMA_FAST_PATH
        h->header.rdma_credit = 0;
#endif
        h->header.ack = 0;

        /* need to add this to the queue */
        viadev_ext_sendq_queue(c, to_use);

        to_use->shandle = NULL;
        to_use->data_start = ((unsigned char *) h) + sizeof(*h);
        to_use->grank = c->global_rank;
        to_use->len = sizeof(*h);
        to_use->bytes_remaining -= to_use->len;
    }
    return to_use;
}


void eager_coalesce(viadev_connection_t * c, char * buf, 
        int len, viadev_packet_envelope * envelope) {
    vbuf * v = NULL;
    viadev_packet_eager_coalesce * h;

    /* get an existing vbuf from a packet waiting already in queue or
     * a new vbuf if first in queue or space not available
     */
    v = get_eager_coalesce_vbuf(c, len, envelope);
    D_PRINT("Got vbuf for coalesced packet: %p\n", v);

    /* update the headers.... */

    h = (viadev_packet_eager_coalesce *) v->buffer;

    h->header.vbuf_credit   += c->local_credit;
    h->header.remote_credit  = c->remote_credit;
    /* Update NR credits */
    if (viadev_use_nfr) {
        h->header.ack += c->pending_acks;
        c->pending_acks = 0;
    }
    c->local_credit   = 0;
#ifdef ADAPTIVE_RDMA_FAST_PATH
    h->header.rdma_credit   += c->rdma_credit;
    c->rdma_credit    = 0;
#endif

    D_PRINT("COALESCE header credit = %d, remote_credit: %d, rdma: %d\n",
            h->header.vbuf_credit, h->header.remote_credit,
            h->header.rdma_credit);

    D_PRINT("Finished with header\n");

    /* if the message we are sending has the same matching information
     * as the previous coalesced send we can omit this information
     */

    if(match_cached_out_envelope(c, envelope)) {
        viadev_packet_eager_coalesce_part * ph = 
            (viadev_packet_eager_coalesce_part *) v->data_start;
        ph->coalesce_type = COALESCED_CACHED;    
        v->data_start = ((unsigned char *) v->data_start) + sizeof(*ph);
        v->len += sizeof(*ph);
        v->bytes_remaining -= sizeof(*ph);
    } else {
        viadev_packet_eager_coalesce_full * ph = 
            (viadev_packet_eager_coalesce_full *) v->data_start;
        ph->coalesce_type = COALESCED_NOT_CACHED;
        COPY_ENVELOPE(&ph->envelope, envelope, c);
        v->data_start = ((unsigned char *) v->data_start) + sizeof(*ph);
        v->len += sizeof(*ph);
        v->bytes_remaining -= sizeof(*ph);
    }


    /* now we can copy the message into the buffer */
    memcpy(v->data_start, buf, len);
    v->data_start = ((unsigned char *) v->data_start) + len;
    v->len += len;
    v->bytes_remaining -= len;

    /* update the message count for this packet */
    h->pkt_count++;

    D_PRINT("Done packing message, pkts: %d, len: %d, id: %d\n", h->pkt_count, v->len,
            h->header.id);
}

/* Since we don't know the final size of the packet until it is 
 * ready to be sent we need to have additional processing  
 * at dequeue time
 */
void prepare_coalesced_pkt(viadev_connection_t * c, vbuf *v) {

#ifdef ADAPTIVE_RDMA_FAST_PATH
    if(BUSY_FLAG == v->padding) {
        int len = v->len;
        VBUF_FLAG_TYPE flag;

        v->desc.u.sr.wr_id = (aint_t) v;
        v->desc.sg_entry.length = 
            v->len + VBUF_FAST_RDMA_EXTRA_BYTES;

        /* update the flags */

        if ((int) *(VBUF_FLAG_TYPE *) (v->buffer + len) == len) {
            flag = (VBUF_FLAG_TYPE) (len + FAST_RDMA_ALT_TAG);
        } else {
            flag = (VBUF_FLAG_TYPE) len;
        }

        /* head */
        *(v->head_flag) = flag;
        /* tail */
        *(VBUF_FLAG_TYPE *) (v->buffer + len) = flag;
        D_PRINT("Sending coalesced over rfp\n");
    } else 
#endif
    {
        D_PRINT("Sending coalesced over send/recv\n");
        vbuf_init_send(v, v->len);
        v->desc.sg_entry.length = v->len;
    }

#ifndef _IBM_EHCA_
    if(v->desc.sg_entry.length < c->max_inline) {
        v->desc.u.sr.send_flags =
            IBV_SEND_SIGNALED | IBV_SEND_INLINE;
    } else 
#endif
    {
        v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    }

#ifdef MEMORY_RELIABLE
    {
        viadev_packet_header *p = (viadev_packet_header *) v->buffer;
        p->crc32 = update_crc(1, (void *) ((char *)VBUF_BUFFER_START(v) +
                    sizeof(viadev_packet_header)),
                v->desc.sg_entry.length - sizeof(viadev_packet_header));
        p->dma_len = v->desc.sg_entry.length - sizeof(viadev_packet_header);
    }
#endif


    D_PRINT("coalesce send len: %d\n", v->desc.sg_entry.length);


    /* should be all set to be sent */
}

