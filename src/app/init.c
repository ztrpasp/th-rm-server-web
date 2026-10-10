/*
 * File: init.c
 * Description: Implementation of initialization routines for RM device
 */
#include "app/init.h"
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <string.h>
#include "vendor/print.h"

struct lcore_conf lcore_conf[RTE_MAX_LCORE];
struct rte_mempool *mbuf_pool;
struct rte_mempool *broadcast_tx_pool = NULL;
bool rm_cli_enabled = false;
unsigned rm_cli_lcore_id = RTE_MAX_LCORE;

void init_eal(int argc, char **argv) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
}

int init_port(uint16_t port_id, struct rte_mempool *pool) {
    struct rte_eth_conf port_conf = {0};
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
    uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;
    int ret;
	struct rte_ether_addr mac_addr;

    ret = rte_eth_dev_configure(port_id, rx_rings, tx_rings, &port_conf);
    if (ret < 0)
        return ret;

    ret = rte_eth_rx_queue_setup(port_id, 0, nb_rxd,
                                 rte_eth_dev_socket_id(port_id), NULL, pool);
    if (ret < 0)
        return ret;

    ret = rte_eth_tx_queue_setup(port_id, 0, nb_txd,
                                 rte_eth_dev_socket_id(port_id), NULL);
    if (ret < 0)
        return ret;

    ret = rte_eth_dev_start(port_id);
    if (ret < 0)
        return ret;

    rte_eth_promiscuous_enable(port_id);

	/* Retrieve and print MAC address */
    rte_eth_macaddr_get(port_id, &mac_addr);
    printf(YELLOW "Port %u MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           port_id,
           mac_addr.addr_bytes[0],
           mac_addr.addr_bytes[1],
           mac_addr.addr_bytes[2],
           mac_addr.addr_bytes[3],
           mac_addr.addr_bytes[4],
           mac_addr.addr_bytes[5]);
    printf(RESET "");
    return 0;
}

void init_lcore_conf(void) {
    unsigned lcore_id;
    unsigned enabled_count = 0;
    memset(lcore_conf, 0, sizeof(lcore_conf));

    RTE_LCORE_FOREACH(lcore_id) {
        if (rte_lcore_is_enabled(lcore_id)) {
            enabled_count++;
        }
    }

    if (enabled_count > 1) {
        rm_cli_enabled = true;
        rm_cli_lcore_id = rte_get_main_lcore();
        printf(YELLOW "[?] lcore %u is reserved for RM CLI\n", rm_cli_lcore_id);
        printf(RESET "");
    } else {
        rm_cli_enabled = false;
        printf(YELLOW "[?] RM CLI is disabled: at least two lcores are required\n");
        printf(RESET "");
    }

    RTE_LCORE_FOREACH(lcore_id) {
        if (rm_cli_enabled && lcore_id == rm_cli_lcore_id) {
            continue;
        }

        if (rte_lcore_is_enabled(lcore_id)) {
            /* Single port (0), single queue (0) */
            lcore_conf[lcore_id].n_rx_queue = 1;
            lcore_conf[lcore_id].rx_queue_list[0].port_id = 0;
            lcore_conf[lcore_id].rx_queue_list[0].queue_id = 0;

            /* TX config: one port, queue 0 */
            lcore_conf[lcore_id].n_tx_port = 1;
            lcore_conf[lcore_id].tx_port_id[0] = 0;
            lcore_conf[lcore_id].tx_queue_id[0] = 0;
        }
    }
}
