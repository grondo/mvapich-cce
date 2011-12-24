#ifndef IB_INIT_H
#define IB_INIT_H

#include "viaparam.h"
#include "ibverbs_const.h"

#define PKEY_MASK 0x7fff /* the last bit is reserved */

/* 
This functions locates PKEY INDEX by PKEY itself
It returns PKEY in the case of success, or int bad_pkey_idx otherwise 
*/
static inline uint16_t get_pkey_index(uint16_t pkey, int port_num) {
    static const uint16_t bad_pkey_idx = -1;
    uint16_t i;
    if(ibv_query_device(viadev.context, &viadev.dev_attr)) {
            error_abort_all(GEN_EXIT_ERR,
                    "Error getting HCA attributes\n");
    }   
    for (i = 0; i < viadev.dev_attr.max_pkeys ; ++i) {
        uint16_t curr_pkey;
        ibv_query_pkey(viadev.context, (uint8_t)port_num, (int)i ,&curr_pkey);
        if (pkey == (ntohs(curr_pkey) & PKEY_MASK)) {
            return i;
        }
    }
    return bad_pkey_idx;
}

/* 
This functions sets PKEY INDEX according to PKEY, if PKEY was defined by user.
*/

static inline void set_pkey_index(uint16_t * pkey_index, int port_num) {
    *pkey_index = (viadev_default_pkey == VIADEV_DEFAULT_PKEY ? viadev_default_pkey_ix :  get_pkey_index(viadev_default_pkey,port_num));
    if (pkey_index < 0 ) {
        error_abort_all(IBV_RETURN_ERR,
            "Can't find PKEY INDEX according to given PKEY\n");
    }
}
#endif  //IB_INIT_H
