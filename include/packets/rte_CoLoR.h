#ifndef _RTE_COLOR_H_
#define _RTE_COLOR_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_mempool.h>

// version
#define COLOR_PROTOCOL 0x7

// package
#define COLOR_ANN_TYPE 0x1
#define COLOR_GET_TYPE 0x2
#define COLOR_DATA_TYPE 0x3
#define COLOR_CONTROL_TYPE 0x4

// length of optional fields (byte)
#define COLOR_PX_LEN 1
#define COLOR_PID_LEN 4

#define COLOR_N_SID_LEN 16
#define COLOR_L_SID_LEN 20
#define COLOR_NID_LEN 16

#define COLOR_PUBLICKEY_FRONT_LEN 2
#define COLOR_QOS_FRONT_LEN 1
#define COLOR_SEG_ID_LEN 4
#define COLOR_HMAC_LEN 4
#define COLOR_MINIMAL_PID_CP_LEN 2

/* ------------------------------Package format of ANN---------------------------------- */
// ANN = front + ANN_unit[unit_num] + (publickey) + (AS_PATH) + PX[px_num]
// flag = F/K/P
struct ANN_front
{
    uint8_t version_package;
    uint8_t ttl;
    uint16_t pkg_len;
    uint16_t checksum;
    uint8_t flag;
    uint8_t unit_px_num;
} __attribute__((__packed__));
typedef struct ANN_front ANN_front_t;

// ANN_unit = front + (N_sid) + (L_sid) + (nid) + ANN_strategy_unit[strategy_num]
struct ANN_unit_front
{
    uint8_t flag;
    uint8_t unit_len;
    uint8_t strategy_num;
} __attribute__((__packed__));
typedef struct ANN_unit_front ANN_unit_front_t;

// ANN_strategy_unit = front + value[length]
struct ANN_strategy_front
{
    uint8_t tag;
    uint8_t len;
} __attribute__((__packed__));
typedef struct ANN_strategy_front ANN_strategy_front_t;

// unit_len 只有 1 字节，单元（含全部策略单元）最长 255 字节
#define ANN_UNIT_MAX_LEN 255
// 每个单元最多解析的策略单元个数（mark_t.strategy_pt 的容量）
#define ANN_MAX_STRATEGY_NUM 32

// ANN_check_format 的返回值
#define ANN_FORMAT_OK 0
#define ANN_FORMAT_ERR_SHORT -1    // 不够 ANN 头，或 pkg_len 放不下 ANN 头和 PX
#define ANN_FORMAT_ERR_PKG_LEN -2  // pkg_len 超过实际收到的字节数
#define ANN_FORMAT_ERR_UNIT -3     // 通告单元的头或 N/L/NID 越界
#define ANN_FORMAT_ERR_STRATEGY -4 // 策略单元越界，或个数超过 ANN_MAX_STRATEGY_NUM

typedef struct ANN_format_info
{
    uint8_t len_mismatch; // unit_len 与按 flag、策略单元实算的长度不一致的单元个数
    uint16_t trailing;    // 最后一个单元之后、PX 之前没被解析到的字节数
} ANN_format_info_t;

/* ------------------------------Package format of GET---------------------------------- */
// GET = front + (publickey) + (QoS) + (Seg_ID) + PID[PID_num] + (PID_RES)
// flag = F/K/Q/S/A
struct GET_front
{
    uint8_t version_package;
    uint8_t ttl;
    uint16_t pkg_len;
    uint16_t checksum;
    uint16_t MTU;
    uint8_t PID_num;
    uint8_t flag;
    uint16_t Minimal_PID_CP;
    uint8_t N_sid[16];
    uint8_t L_sid[20];
    uint8_t nid[16];
} __attribute__((__packed__));
typedef struct GET_front GET_front_t;

/* ------------------------------Package format of DATA---------------------------------- */
// DATA = front + (QoS) + (HMAC) + (Seg_ID) + (Minimal_PID_CP) + PID[PID_num]
// flag = F/B/R/M/Q/C/S
struct DATA_front
{
    uint8_t version_package;
    uint8_t ttl;
    uint16_t pkg_len;
    uint16_t checksum;
    uint8_t header_len;
    uint8_t PID_pt;
    uint8_t PID_num;
    uint8_t flag;
    uint8_t N_sid[16];
    uint8_t L_sid[20];
    uint8_t nid[16];
} __attribute__((__packed__));
typedef struct DATA_front DATA_front_t;

struct CONTROL_front
{
    uint8_t version_package;
    uint8_t ttl;
    uint16_t checksum;
    uint8_t header_len;
	uint8_t tag;
	uint16_t len;
} __attribute__((__packed__));
typedef struct CONTROL_front CONTROL_front_t;

/* ------------------------------Function of CoLoR---------------------------------- */

#define DEFAULT_TTL 128

#define GET_PROTOCOL_TYPE(x) (((x)&0xf0)>>4)
#define GET_PACKAGE_TYPE(x) ((x)&0x0f)

#define GET_ANN_UNIT_NUM(x) ((x)>>4)
#define ADD_ANN_UNIT_NUM(x) ((x)=(x)+0x10)
#define GET_ANN_PX_NUM(x) ((x)&0x0f)
#define ADD_ANN_PX_NUM(x) ((x)=(x)+0x01)

#define CHECK_ANN_UNIT_FLAG_N(flag) (((flag)&0x80)==0x80)
#define CHECK_ANN_UNIT_FLAG_L(flag) (((flag)&0x40)==0x40)
#define CHECK_ANN_UNIT_FLAG_I(flag) (((flag)&0x20)==0x20)
#define GET_ANN_UNIT_FLAG_AM(flag) (((flag)<<3)>>6)
#define SET_ANN_UNIT_FLAG_N(flag) ((flag)=((flag)|0x80))
#define SET_ANN_UNIT_FLAG_L(flag) ((flag)=((flag)|0x40))
#define SET_ANN_UNIT_FLAG_I(flag) ((flag)=((flag)|0x20))

#define CHECK_FLAG_F(flag) (((flag)&0x80)==0x80)
#define REVERSE_FLAG_F(flag) ((flag)=((flag)^0x80))
#define CHECK_GET_FLAG_R(flag) ((flag) & 0x04)
#define SET_GET_FLAG_R(flag) ((flag) | 0x04)

#define CONTROL_UNKNOWN_TAG 0
// stage 1 & 2
#define CONTROL_CONFIG_RM 1
#define ACK_CONTROL_CONFIG_RM 2
#define CONTROL_CONFIG_BR 3
#define ACK_CONTROL_CONFIG_BR 4
// stage 3
#define CONTROL_REG_PROXY 5
#define CONTROL_RET_PROXY 6
#define CONTROL_ACK_PROXY 7
#define CONTROL_SYN_CONFIG_PROXY 8
#define ACK_CONTROL_SYN_CONFIG_PROXY 9
#define CONTROL_SID_LIST 40

// strategy info
#define CONTROL_SYN_BR_SID 16
#define CONTROL_WARN_DATA_LEAKAGE 17
#define CONTROL_WARN_OUTER_ATTACK 18
#define CONTROL_GLOBAL_TOPOLOGY_INFO 19

// strategy control
#define CONTROL_STRATEGY_GLOBAL 10
#define CONTROL_STRATEGY_GLOBAL_ACK 11
#define CONTROL_STRATEGY_BR 12
#define CONTROL_STRATEGY_BR_ACK 13

// stat
#define STAT_ANN_INFO 31
#define RET_STAT_ANN_INFO 32
#define STAT_GET_INFO 33
#define RET_STAT_GET_INFO 34
#define STAT_PROXY_INFO 35
#define RET_STAT_PROXY_INFO 36

// strategy
#define STRATEGY_SECURITY_LEVEL 1
#define STRATEGY_SECURITY_LEVEL_CHECK 3
#define STRATEGY_ANNOUNCE_PATH 2
#define STRATEGY_ANNOUNCE_PATH_CHECK 4
#define STRATEGY_DATA_LEAKAGE_CHECK 5
#define STRATEGY_N_SID_FIELD_OMIT 6
#define STRATEGY_L_SID_FIELD_OMIT 7
#define STRATEGY_NID_FIELD_OMIT 8
#define STRATEGY_AS_SECURITY_CHECK 9
#define STRATEGY_TRACE_EVERY_PKG 10
#define STRATEGY_ANN_PATH_DEFAULT 11
#define STRATEGY_ANN_PATH_STP 12
// 随 ANN 携带，见 docs/design-ann-sid-desc.md。tag 号与上面的开关共用，新增的不要超过 30
#define STRATEGY_SID_NAME 13 // SID 名称：L_SID 的哈希原文，SHA1(名称) == L_SID
#define STRATEGY_SID_DESC 14 // SID 描述：协议/服务名，只用于展示

// ack
#define ACK_SUCCESS 1

// tool
void show_segment(uint8_t *buf, int len);
void to_bytes(uint32_t val, uint8_t *bytes);
uint32_t to_int32(const uint8_t *bytes);

void ANN_init(uint8_t *buf);
int ANN_update_checksum(uint8_t *buf);
int ANN_check_checksum(uint8_t *buf);
int ANN_check_format(const uint8_t *buf, uint32_t avail, ANN_format_info_t *info);
const char *ANN_format_error_str(int code);
void ANN_append_unit(uint8_t *buf, uint8_t *unit);
void ANN_unit_construct(uint8_t *ubuf, uint8_t *n_sid, uint8_t *l_sid, uint8_t *nid);
int ANN_unit_append_strategy(uint8_t *ubuf, uint8_t t, uint8_t l, uint8_t *v);
void ANN_append_px(uint8_t *buf, uint16_t px);
int ANN_get_pkg_length(uint8_t *buf);

void GET_init(uint8_t *buf, uint8_t *n_sid, uint8_t *l_sid, uint8_t *nid);
void GET_append_pid(uint8_t *buf, uint32_t pid);
void GET_append_rn(uint8_t *buf, uint32_t rn);
uint32_t GET_pids(uint8_t *buf, uint8_t *pids);
uint32_t GET_calculate_PID(uint8_t *pkt, uint32_t sk, uint16_t to_px);
void GET_update_checksum(uint8_t *buf);
int GET_check_checksum(uint8_t *buf);

void DATA_init(uint8_t *buf, uint8_t *n_sid, uint8_t *l_sid, uint8_t *nid);
void DATA_append_pid(uint8_t *buf, uint32_t pid);
void DATA_append_data(uint8_t *buf, uint8_t *data, int len);
void DATA_update_checksum(uint8_t *buf);
int DATA_check_checksum(uint8_t *buf);

void CONTROL_update_checksum(uint8_t *buf);
int CONTROL_check_checksum(uint8_t *buf);

int CoLoR_get_pkg_protocol(uint8_t *buf);
int CoLoR_get_pkg_type(uint8_t *buf);
int CoLoR_get_pkg_length(uint8_t *buf);
void CoLoR_show_pkg(uint8_t *buf);

void md5(const uint8_t *initial_msg, size_t initial_len, uint8_t *digest);
void CONTROL_init(uint8_t *, uint8_t , uint16_t , uint8_t *);

#endif