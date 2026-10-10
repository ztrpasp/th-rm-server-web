/*
 * File: init.h
 * Description: Initialization of lcore_conf for RM device (single port, queue 0)
 */
#ifndef __INIT_H__
#define __INIT_H__

#include <stdbool.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#define RTE_LOGTYPE_RM RTE_LOGTYPE_USER1

#define RTE_TEST_RX_DESC_DEFAULT 1024
#define RTE_TEST_TX_DESC_DEFAULT 1024

#define MAX_PKT_BURST 32
#define BURST_TX_DRAIN_US 100

#define MEMPOOL_CACHE_SIZE 256
#define MAX_RX_QUEUE_PER_LCORE 16

struct mbuf_table {
    uint16_t len;
    struct rte_mbuf *m_table[MAX_PKT_BURST];
};

struct lcore_rx_queue {
    uint16_t port_id;
    uint8_t queue_id;
} __rte_cache_aligned;

struct lcore_conf {
    uint16_t n_rx_queue;
    struct lcore_rx_queue rx_queue_list[MAX_RX_QUEUE_PER_LCORE];
    uint16_t n_tx_port;
    uint16_t tx_port_id[RTE_MAX_ETHPORTS];
    uint16_t tx_queue_id[RTE_MAX_ETHPORTS];
    struct mbuf_table tx_mbufs[RTE_MAX_ETHPORTS];
} __rte_cache_aligned;

extern struct lcore_conf lcore_conf[RTE_MAX_LCORE];
extern struct rte_mempool *mbuf_pool;
extern struct rte_mempool *broadcast_tx_pool;
extern bool rm_cli_enabled;
extern unsigned rm_cli_lcore_id;

void init_eal(int argc, char **argv);
int init_port(uint16_t port_id, struct rte_mempool *pool);
void init_lcore_conf(void);

#endif /* __INIT_H__ */
