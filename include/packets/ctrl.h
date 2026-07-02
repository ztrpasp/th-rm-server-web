//
// Created by lcf2001523@gmail.com on 25-5-16.
//

#ifndef CTRL_H
#define CTRL_H

#include "packets/rte_CoLoR.h"
#include "process/rm_pkt_process.h"
#include "process/rm_send.h"
#include "tables/rm_tables.h"
#include "packets/pkt.h"

extern struct rte_mempool *broadcast_tx_pool;

void
process_control_packet(struct rte_mbuf *m, uint8_t portid, struct lcore_conf *qconf);

#endif //CTRL_H
