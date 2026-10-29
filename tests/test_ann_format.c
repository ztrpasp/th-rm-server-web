/*
 * ANN 格式校验（ANN_check_format）与转发时拼单元（ANN_unit_append_strategy）的单元测试。
 * 测试向量来自 docs/design-ann-sid-desc.md §2.6。
 */
#include <assert.h>
#include <string.h>

#include "packets/rte_CoLoR.h"

// rte_CoLoR.c 的打印函数要用 cli_print；测试不链接命令行模块，这里给一个空实现
void
cli_print(const char *fmt, ...)
{
    (void)fmt;
}

// 92 字节 ANN：提供端 NID a1×16，NAME tcp://10.10.9.2:3306，DESC mysql，AM=1
static const char *const ann_vector_hex =
    "71405c0092910010e85402a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1d992c50089cf6b0490bcff0dbf65bdede57e4e64"
    "a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a10d147463703a2f2f31302e31302e392e323a333330360e056d7973716c";

#define VECTOR_LEN 92
// 报文内偏移
#define OFF_UNIT_PX_NUM 7
#define OFF_UNIT 8
#define OFF_UNIT_LEN 9
#define OFF_STRATEGY_NUM 10
#define OFF_NAME_TLV 63
#define OFF_DESC_TLV 85

static int
hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    return c - 'a' + 10;
}

// 把测试向量读进 buf（buf 至少 VECTOR_LEN + 16 字节，多出的部分清零）
static void
load_vector(uint8_t *buf, size_t cap)
{
    assert(strlen(ann_vector_hex) == VECTOR_LEN * 2);
    assert(cap >= VECTOR_LEN);
    memset(buf, 0, cap);
    for (size_t i = 0; i < VECTOR_LEN; i++)
        buf[i] = (uint8_t)(hex_value(ann_vector_hex[2 * i]) << 4 |
            hex_value(ann_vector_hex[2 * i + 1]));
}

static void
set_pkg_len(uint8_t *buf, uint16_t len)
{
    ((ANN_front_t *)buf)->pkg_len = len;
}

static void
test_vector_is_valid(void)
{
    uint8_t buf[128];
    ANN_format_info_t info;

    load_vector(buf, sizeof(buf));
    assert(((ANN_front_t *)buf)->pkg_len == VECTOR_LEN);
    assert(ANN_check_checksum(buf) == 1);
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_OK);
    assert(info.len_mismatch == 0);
    assert(info.trailing == 0);
    // mbuf 里比 pkg_len 多出来的字节（例如以太网填充）不影响
    assert(ANN_check_format(buf, sizeof(buf), &info) == ANN_FORMAT_OK);
    // info 可以不要
    assert(ANN_check_format(buf, VECTOR_LEN, NULL) == ANN_FORMAT_OK);
}

static void
test_length_fields(void)
{
    uint8_t buf[128];
    ANN_format_info_t info;

    load_vector(buf, sizeof(buf));
    assert(ANN_check_format(NULL, VECTOR_LEN, &info) == ANN_FORMAT_ERR_SHORT);
    assert(ANN_check_format(buf, sizeof(ANN_front_t) - 1, &info) == ANN_FORMAT_ERR_SHORT);
    // pkg_len 比实际收到的长
    assert(ANN_check_format(buf, VECTOR_LEN - 1, &info) == ANN_FORMAT_ERR_PKG_LEN);
    // pkg_len 比 ANN 头还短
    set_pkg_len(buf, sizeof(ANN_front_t) - 1);
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_SHORT);
    // 只有 ANN 头，却声明了 2 个 PX
    load_vector(buf, sizeof(buf));
    buf[OFF_UNIT_PX_NUM] = 0x02;
    set_pkg_len(buf, sizeof(ANN_front_t) + 2);
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_SHORT);
}

static void
test_px_list(void)
{
    uint8_t buf[128];
    ANN_format_info_t info;

    // 声明 1 个 PX 但没留出位置：PX 占掉 DESC 的最后 2 字节，DESC 越界
    load_vector(buf, sizeof(buf));
    buf[OFF_UNIT_PX_NUM] = 0x11;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_STRATEGY);

    // 在末尾补上 PX，合法
    buf[VECTOR_LEN] = 0x34;
    buf[VECTOR_LEN + 1] = 0x12;
    set_pkg_len(buf, VECTOR_LEN + 2);
    assert(ANN_check_format(buf, VECTOR_LEN + 2, &info) == ANN_FORMAT_OK);
    assert(info.trailing == 0);
}

static void
test_unit_bounds(void)
{
    uint8_t buf[128];
    ANN_format_info_t info;

    // 声明 2 个单元，第二个单元连头都放不下
    load_vector(buf, sizeof(buf));
    buf[OFF_UNIT_PX_NUM] = 0x20;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_UNIT);

    // 包在 L_SID 中间截断：N/L/NID 放不下
    load_vector(buf, sizeof(buf));
    set_pkg_len(buf, OFF_UNIT + 3 + 16 + 10);
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_UNIT);
}

static void
test_strategy_bounds(void)
{
    uint8_t buf[128];
    ANN_format_info_t info;

    // DESC 的 len 多写 1
    load_vector(buf, sizeof(buf));
    buf[OFF_DESC_TLV + 1] = 6;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_STRATEGY);

    // NAME 的 len 写成 255，远超包长
    load_vector(buf, sizeof(buf));
    buf[OFF_NAME_TLV + 1] = 255;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_STRATEGY);

    // strategy_num 比实际多 1：第三个策略单元连 tag/len 都读不到
    load_vector(buf, sizeof(buf));
    buf[OFF_STRATEGY_NUM] = 3;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_ERR_STRATEGY);

    // 只剩 1 个字节：tag 能读，len 读不到
    set_pkg_len(buf, VECTOR_LEN + 1);
    assert(ANN_check_format(buf, VECTOR_LEN + 1, &info) == ANN_FORMAT_ERR_STRATEGY);

}

// 把向量里的两个 TLV 换成 count 个 len = 0 的策略单元（每个 2 字节），返回新的 pkg_len
static uint16_t
build_empty_strategies(uint8_t *buf, size_t cap, int count)
{
    uint16_t pkg_len = (uint16_t)(OFF_NAME_TLV + 2 * count);
    load_vector(buf, cap);
    assert(pkg_len <= cap);
    for (int i = 0; i < count; i++) {
        buf[OFF_NAME_TLV + 2 * i] = 0x20;
        buf[OFF_NAME_TLV + 2 * i + 1] = 0;
    }
    buf[OFF_STRATEGY_NUM] = (uint8_t)count;
    buf[OFF_UNIT_LEN] = (uint8_t)(pkg_len - OFF_UNIT);
    set_pkg_len(buf, pkg_len);
    return pkg_len;
}

static void
test_strategy_count_limit(void)
{
    uint8_t buf[192];
    ANN_format_info_t info;
    uint16_t pkg_len;

    // 32 个正好是 mark_t 的容量
    pkg_len = build_empty_strategies(buf, sizeof(buf), ANN_MAX_STRATEGY_NUM);
    assert(ANN_check_format(buf, pkg_len, &info) == ANN_FORMAT_OK);
    assert(info.len_mismatch == 0);
    assert(info.trailing == 0);

    // 33 个：每个都在包内，但 mark_t 放不下
    pkg_len = build_empty_strategies(buf, sizeof(buf), ANN_MAX_STRATEGY_NUM + 1);
    assert(ANN_check_format(buf, pkg_len, &info) == ANN_FORMAT_ERR_STRATEGY);
}

static void
test_tolerated_inconsistencies(void)
{
    uint8_t buf[128];
    ANN_format_info_t info;

    // unit_len 填错：以实算为准，记一次不一致
    load_vector(buf, sizeof(buf));
    buf[OFF_UNIT_LEN] = 0x50;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_OK);
    assert(info.len_mismatch == 1);
    assert(info.trailing == 0);

    // strategy_num 少写 1：DESC 没被解析，剩下 7 字节；unit_len 也就对不上了
    load_vector(buf, sizeof(buf));
    buf[OFF_STRATEGY_NUM] = 1;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_OK);
    assert(info.len_mismatch == 1);
    assert(info.trailing == 2 + 5);

    // len = 0 的策略单元格式上合法
    load_vector(buf, sizeof(buf));
    buf[OFF_DESC_TLV + 1] = 0;
    set_pkg_len(buf, OFF_DESC_TLV + 2);
    buf[OFF_UNIT_LEN] = OFF_DESC_TLV + 2 - OFF_UNIT;
    assert(ANN_check_format(buf, VECTOR_LEN, &info) == ANN_FORMAT_OK);
    assert(info.len_mismatch == 0);
}

static void
test_append_strategy_limit(void)
{
    uint8_t ubuf[ANN_UNIT_MAX_LEN + 1];
    uint8_t n_sid[16] = {0}, l_sid[20] = {0}, nid[16] = {0}, value[255];
    ANN_unit_front_t *unit = (ANN_unit_front_t *)ubuf;

    memset(value, 'x', sizeof(value));

    // 55 + (2 + 198) = 255，正好放下
    ANN_unit_construct(ubuf, n_sid, l_sid, nid);
    assert(unit->unit_len == 55);
    assert(ANN_unit_append_strategy(ubuf, 13, 198, value) == 1);
    assert(unit->unit_len == 255);
    assert(unit->strategy_num == 1);
    // 再加 1 字节就超了：不追加，单元不变
    assert(ANN_unit_append_strategy(ubuf, 14, 1, value) == 0);
    assert(unit->unit_len == 255);
    assert(unit->strategy_num == 1);

    // 55 + (2 + 199) = 256，放不下
    ANN_unit_construct(ubuf, n_sid, l_sid, nid);
    assert(ANN_unit_append_strategy(ubuf, 13, 199, value) == 0);
    assert(unit->unit_len == 55);
    assert(unit->strategy_num == 0);

    // 参数无效
    assert(ANN_unit_append_strategy(ubuf, 13, 0, value) == -1);
    assert(ANN_unit_append_strategy(ubuf, 13, 1, NULL) == -1);
    assert(ANN_unit_append_strategy(NULL, 13, 1, value) == -1);
    assert(unit->unit_len == 55);
}

// 转发时按 N/L/NID + 全部 TLV 重组单元，除 flag 的 AM 位外应与原单元逐字节一致
static void
test_forward_rebuild_keeps_tlvs(void)
{
    uint8_t buf[128];
    uint8_t ubuf[ANN_UNIT_MAX_LEN + 1];
    ANN_unit_front_t *unit = (ANN_unit_front_t *)ubuf;

    load_vector(buf, sizeof(buf));
    uint8_t *src = buf + OFF_UNIT;
    ANN_unit_construct(ubuf, src + 3, src + 3 + 16, src + 3 + 16 + 20);
    assert(ANN_unit_append_strategy(ubuf, buf[OFF_NAME_TLV], buf[OFF_NAME_TLV + 1],
        buf + OFF_NAME_TLV + 2) == 1);
    assert(ANN_unit_append_strategy(ubuf, buf[OFF_DESC_TLV], buf[OFF_DESC_TLV + 1],
        buf + OFF_DESC_TLV + 2) == 1);

    assert(unit->unit_len == src[1]);
    assert(unit->strategy_num == src[2]);
    // 重组时 flag 只置 N/L/I，AM 位不保留（RM 目前不看 AM）
    assert(unit->flag == (src[0] & 0xe0));
    assert(memcmp(ubuf + 1, src + 1, unit->unit_len - 1) == 0);
}

int
main(void)
{
    test_vector_is_valid();
    test_length_fields();
    test_px_list();
    test_unit_bounds();
    test_strategy_bounds();
    test_strategy_count_limit();
    test_tolerated_inconsistencies();
    test_append_strategy_limit();
    test_forward_rebuild_keeps_tlvs();
    return 0;
}
