/*
 * File: main.c
 * Description: RM application entry point
 */
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <rte_lcore.h>
#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_log.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_hash.h>
#include "app/init.h"
#include "process/rm_send.h"
#include "process/rm_main_function.h"
#include "tables/rm_tables.h"
#include "packets/rte_CoLoR.h"
#include "tables/rm_routing_table.h"
#include "commandline/cli.h"
#include "vendor/print.h"
#include "telemetry/rm_log.h"
#include "telemetry/rm_metrics.h"
#include "telemetry/rm_web.h"

/* Idle sleep (meson option idle_sleep_us, default 0 = pure busy polling as on
 * the physical deployment). When > 0, the worker sleeps that many microseconds
 * after RM_IDLE_SPIN_ROUNDS consecutive empty polls, so idle RMs in the
 * container lab do not keep a whole CPU at 100%. */
#ifndef RM_IDLE_SLEEP_US
#define RM_IDLE_SLEEP_US 0
#endif
#define RM_IDLE_SPIN_ROUNDS 64

#define MEMPOOL_NAME "MBUF_POOL"
#define NB_MBUF 8192

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250

_Atomic bool force_quit;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        atomic_store_explicit(&force_quit, true, memory_order_release);
    }
}

void parse_global_as_config(const char *filename) {
	printf(BLUE "[>] Start to read global as config info\n");
    printf(RESET "");
	FILE *fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "无法打开文件 \"%s\": %s\n", filename, strerror(errno));
		return;
	}

	char linebuf[256];
	while (fgets(linebuf, sizeof(linebuf), fp) != NULL) {
		// 1. 去掉行首空白
		char *p = linebuf;
		while (*p != '\0' && isspace((unsigned char)*p)) {
			p++;
		}
		// 2. 如果第一个字符不是 '$'，则跳过
		if (*p != '$') {
			continue;
		}
		// p 指向 '$'，跳过它
		p++;

		// 3. 跳过 '$' 之后可能的空格
		while (*p != '\0' && isspace((unsigned char)*p)) {
			p++;
		}

		// 4. 使用 sscanf 提取 aid1, aid2, px（十六进制）
		//
		//    我们这里先用 unsigned int 中转，再强制转换为 uint8_t/uint16_t
		//    这样可以避免 "%hhu"、"%hx" 在某些平台上的兼容性问题。
		unsigned int tmp_aid1, tmp_aid2, tmp_px;
		int n = sscanf(p, "%u %u 0x%x", &tmp_aid1, &tmp_aid2, &tmp_px);
		if (n == 3) {
			// 强制转换为目标类型
			uint8_t aid1 = (uint8_t)tmp_aid1;
			uint8_t aid2 = (uint8_t)tmp_aid2;
			uint16_t px   = (uint16_t)tmp_px;

			// 调用 graph_add
			graph_add(my_as_info->as_graph, aid1, aid2, px);
		}
		else {
			// 解析失败，可以根据需要打印警告或直接跳过
			fprintf(stderr, "警告: 无法解析这一行: %s", linebuf);
			continue;
		}
	}

	fclose(fp);
	printf(GREEN "[+] global as config file read ssccessfully\n");
    printf(RESET "");
}

static int
worker_main(__attribute__((unused)) void *arg) {
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
    unsigned lcore_id;
    uint64_t prev_tsc, diff_tsc, cur_tsc;
    int i, nb_rx;
    uint8_t queueid;
	uint16_t portid;
    struct lcore_conf *qconf;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) /
		US_PER_S * BURST_TX_DRAIN_US;

    lcore_id = rte_lcore_id();
    qconf = &lcore_conf[lcore_id];
    const uint16_t n_rx_q = qconf->n_rx_queue;
	const uint16_t n_tx_p = qconf->n_tx_port;

    if (n_rx_q == 0) {
		// RTE_LOG(INFO, L3FWD, "lcore %u has nothing to do\n", lcore_id);
		printf(YELLOW "[?] lcore %u has nothing to do\n",lcore_id);
        printf(RESET "");
        return 0;
	}
    printf("[X] Entering main loop on lcore %u\n",lcore_id);

    for(i = 0; i < n_rx_q; i++) {
        portid = qconf->rx_queue_list[i].port_id;
        queueid = qconf->rx_queue_list[i].queue_id;
        printf("[X] lcore %u is receiving packets from port %u, queue %u\n",lcore_id,portid,queueid);
    }

    cur_tsc = rte_rdtsc();
	prev_tsc = cur_tsc;
#if RM_IDLE_SLEEP_US > 0
    unsigned idle_rounds = 0;
#endif

    while (!atomic_load_explicit(&force_quit, memory_order_acquire)) {
#if RM_IDLE_SLEEP_US > 0
        bool got_packets = false;
#endif
    	/* 清空TX队列(发包) */
        diff_tsc = cur_tsc - prev_tsc;
        if(unlikely(diff_tsc > drain_tsc)) {
        	for(i = 0; i < n_tx_p; i++) {
                portid = qconf->tx_port_id[i];
                if (qconf->tx_mbufs[portid].len == 0) continue;
                send_burst(qconf, qconf->tx_mbufs[portid].len, portid);
                qconf->tx_mbufs[portid].len = 0;
            }

            prev_tsc = cur_tsc;
        }

        /* 从RX队列中读取数据包 */
        for (i = 0; i < n_rx_q; ++i) {
            portid = qconf->rx_queue_list[i].port_id;
			queueid = qconf->rx_queue_list[i].queue_id;
			nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst,
				MAX_PKT_BURST);
            if (nb_rx == 0) continue;
#if RM_IDLE_SLEEP_US > 0
            got_packets = true;
#endif
#ifdef DEBUG_PRINT_IP_HEADER
			/* print ip header */
			for (int j = 0; j < nb_rx; j++) {
                struct rte_mbuf *m = pkts_burst[j];
                struct rte_ether_hdr *eth =
                    rte_pktmbuf_mtod(m, struct rte_ether_hdr *);

                /* 只处理 IPv4 */
                if (eth->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
                    /* IP 头紧跟在以太头之后 */
                    struct rte_ipv4_hdr *ip =
                        (struct rte_ipv4_hdr *)( (char*)eth + sizeof(*eth) );
                    uint32_t s = rte_be_to_cpu_32(ip->src_addr);
                    uint32_t d = rte_be_to_cpu_32(ip->dst_addr);
                    printf("lcore %u port %u pkt #%d: "
                           "%u.%u.%u.%u -> %u.%u.%u.%u\n",
                           lcore_id, portid, j,
                           (s >> 24) & 0xFF, (s >> 16) & 0xFF,
                           (s >>  8) & 0xFF,  s        & 0xFF,
                           (d >> 24) & 0xFF, (d >> 16) & 0xFF,
                           (d >>  8) & 0xFF,  d        & 0xFF);
                }
            }
#endif
        rm_process_packets(nb_rx, pkts_burst, portid, qconf);
        }
#if RM_IDLE_SLEEP_US > 0
        if (got_packets) {
            idle_rounds = 0;
        } else if (++idle_rounds >= RM_IDLE_SPIN_ROUNDS) {
            rte_delay_us_sleep(RM_IDLE_SLEEP_US);
        }
#endif
        cur_tsc = rte_rdtsc();
    }

    return 0;

}

int
read_control_rm_config(char *filename, uint8_t *read_buf)
{
	if(access(filename, F_OK) != 0)
	{
		printf(YELLOW "[?] No config file found. ret.\n");
        printf(RESET "");
		return -1;
	}

	int check, offset = 0;
	FILE *fp = fopen(filename, "rb");
	if(fp == NULL)
	{
		printf(RED "[!] Unable to open the file\n");
        printf(RESET "");
		return -1;
	}

	for(;;)
	{
		check = fread(read_buf + offset, 1, 256, fp);
		if(check < 256)
			break;
		else
			offset += check;
	}
	fclose(fp);
	return 0;
}

// prepare data for RM
int rm_prepare()
{
	if (ip_mac_store_initialize() != 0 || unsend_buf_initialize() != 0)
		return -1;
	if (rm_data_initialize() != 0)
		return -1;

	read_ip_config("./rm_config");

	uint8_t read_buf[4096];
	for(int portid = 0; portid < 32; portid++)
	{
		if(my_as_info->port_ip[portid] != 0)
		{
			printf(BLUE "[>] Try to read config_recieve_file with P %d\n", portid);
            printf(RESET "");
			if(0 == read_control_rm_config("./config_recieve_file", read_buf))
			{
				printf(BLUE "[>] Read config_recieve_file with P %d\n", portid);
                printf(RESET "");

				rm_CONTROL_process(read_buf, portid);
				as_store_show();
				basic_info_show();
			}
			break;
		}
	}
	return 0;
}

int init_broadcast_tx_pool(int nb_ports) {
    broadcast_tx_pool = rte_pktmbuf_pool_create("BROADCAST_TX_POOL", NUM_MBUFS * nb_ports,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if(broadcast_tx_pool == NULL)
        return -1;
    return 0;
}

int main(int argc, char **argv) {
    CLR_SCREEN();
    printf(" $$$$$$\\            $$\\                 $$$$$$$\\        $$$$$$$\\  $$\\      $$\\ \n");
    printf("$$  __$$\\           $$ |                $$  __$$\\       $$  __$$\\ $$$\\    $$$ |\n");
    printf("$$ /  \\__| $$$$$$\\  $$ |       $$$$$$\\  $$ |  $$ |      $$ |  $$ |$$$$\\  $$$$ |\n");
    printf("$$ |      $$  __$$\\ $$ |      $$  __$$\\ $$$$$$$  |      $$$$$$$  |$$\\$$\\$$ $$ |\n");
    printf("$$ |      $$ /  $$ |$$ |      $$ /  $$ |$$  __$$<       $$  __$$< $$ \\$$$  $$ |\n");
    printf("$$ |  $$\\ $$ |  $$ |$$ |      $$ |  $$ |$$ |  $$ |      $$ |  $$ |$$ |\\$  /$$ |\n");
    printf("\\$$$$$$  |\\$$$$$$  |$$$$$$$$\\ \\$$$$$$  |$$ |  $$ |      $$ |  $$ |$$ | \\_/ $$ |\n");
    printf(" \\______/  \\______/ \\________| \\______/ \\__|  \\__|      \\__|  \\__|\\__|     \\__|\n");
    printf(GREEN "欢迎使用共生网络资源管理器！\n");
    printf(RESET "");
    int ret;

    atomic_init(&force_quit, false);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    init_eal(argc, argv);
    rm_metrics_init();
    if (rm_log_init() != 0)
        rte_exit(EXIT_FAILURE, "Cannot initialize RM structured logging\n");
    RM_LOG_INFO(RM_LOG_MODULE_APP, RM_EVENT_APP_STARTED, RM_LOG_PORT_NONE,
        "RM process started");

    mbuf_pool = rte_pktmbuf_pool_create(MEMPOOL_NAME, NB_MBUF,
                                        MEMPOOL_CACHE_SIZE, 0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                        rte_socket_id());
    if (mbuf_pool == NULL) {
        RM_LOG_ERROR(RM_LOG_MODULE_APP, RM_EVENT_TABLE_INITIALIZATION_FAILED,
            RM_LOG_PORT_NONE, "main packet pool initialization failed");
        rm_log_shutdown();
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    }

    ret = init_port(0, mbuf_pool);
    if (ret != 0) {
        RM_LOG_ERROR(RM_LOG_MODULE_APP, RM_EVENT_TABLE_INITIALIZATION_FAILED,
            0, "network port initialization failed: ret=%d", ret);
        rm_log_shutdown();
        rte_eal_cleanup();
        rte_exit(EXIT_FAILURE, "Cannot initialize network port\n");
    }
    init_lcore_conf();
    if (init_broadcast_tx_pool(1) != 0) {
        RM_LOG_ERROR(RM_LOG_MODULE_APP, RM_EVENT_TABLE_INITIALIZATION_FAILED,
            RM_LOG_PORT_NONE, "broadcast packet pool initialization failed");
        rm_log_shutdown();
        rte_exit(EXIT_FAILURE, "Cannot create broadcast tx pool\n");
    }


    if (rm_prepare() != 0) {
        RM_LOG_ERROR(RM_LOG_MODULE_APP, RM_EVENT_TABLE_INITIALIZATION_FAILED,
            RM_LOG_PORT_NONE, "RM runtime data initialization failed");
        rm_log_shutdown();
        rte_exit(EXIT_FAILURE, "Cannot initialize RM runtime data\n");
    }
    if (rm_web_start() != 0) {
        RM_LOG_ERROR(RM_LOG_MODULE_APP, RM_EVENT_WEB_START_FAILED,
            RM_LOG_PORT_NONE, "web monitor initialization failed");
        rm_log_shutdown();
        rte_exit(EXIT_FAILURE, "Cannot initialize RM web monitor\n");
    }
    printf(BLUE "[>] Initialize strategy..\n");
    printf(RESET "");
	global_quick_set_strategy(STRATEGY_SECURITY_LEVEL, true);
	global_quick_set_strategy(STRATEGY_SECURITY_LEVEL_CHECK, true);
	global_quick_set_strategy(STRATEGY_ANNOUNCE_PATH, true);
	global_quick_set_strategy(STRATEGY_ANNOUNCE_PATH_CHECK, true);
	global_quick_set_strategy(STRATEGY_DATA_LEAKAGE_CHECK, true);
    printf(BLUE "[>] Add graph info..\n");
    printf(RESET "");

	// my_as_info->as_graph = graph_create(4);
    // graph_add(my_as_info->as_graph, 1, 2, 0x5566);
	// graph_add(my_as_info->as_graph, 1, 3, 0x3344);
	// graph_add(my_as_info->as_graph, 1, 15, 0x1122);

    parse_global_as_config("global_as_config");
    load_routes_from_file("ip_route_static");
    tsid_table_init();

	graph_show(my_as_info->as_graph);
    /* 启动所有工作核 */
    RTE_LCORE_FOREACH_WORKER(ret) {
        if (rm_cli_enabled && (unsigned)ret == rm_cli_lcore_id) {
            rte_eal_remote_launch(cli_main_loop, NULL, ret);
        } else {
            rte_eal_remote_launch(worker_main, NULL, ret);
        }
    }

    /* 主核也跑 worker */
    if (rm_cli_enabled && rte_lcore_id() == rm_cli_lcore_id) {
        cli_main_loop(NULL);
    } else {
        worker_main(NULL);
    }

    rte_eal_mp_wait_lcore();

    rm_web_stop();
    RM_LOG_INFO(RM_LOG_MODULE_APP, RM_EVENT_APP_STOPPING, RM_LOG_PORT_NONE,
        "RM process stopping");
    rm_log_shutdown();
    if (my_as_info != NULL && my_as_info->my_status != NULL) {
        status_destroy(my_as_info->my_status);
        my_as_info->my_status = NULL;
    }
    rte_eth_dev_stop(0);
    rte_eth_dev_close(0);
    rte_eal_cleanup();

    return 0;
}
