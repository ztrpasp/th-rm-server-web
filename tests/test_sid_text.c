/*
 * SHA-1（校验 SID 名称用）与展示文本清洗的单元测试。
 * SID 名称向量来自 docs/design-ann-sid-desc.md §2.6，其余是 FIPS 180 的标准向量。
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vendor/display_text.h"
#include "vendor/sha1.h"

static void
check_sha1(const void *data, size_t len, const char *expected_hex)
{
    uint8_t digest[RM_SHA1_DIGEST_LEN];
    char hex[RM_SHA1_DIGEST_LEN * 2 + 1];

    rm_sha1(data, len, digest);
    for (int i = 0; i < RM_SHA1_DIGEST_LEN; i++)
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    assert(strcmp(hex, expected_hex) == 0);
}

static void
check_sha1_text(const char *text, const char *expected_hex)
{
    check_sha1(text, strlen(text), expected_hex);
}

static void
test_sha1(void)
{
    // §2.6：SHA1(NAME) = L_SID；53 字节的那条补位后正好一个块
    check_sha1_text("tcp://10.10.9.2:3306", "d992c50089cf6b0490bcff0dbf65bdede57e4e64");
    check_sha1_text("http://10.10.9.2:8000/api/items",
        "b16b4f7c8a252c6c03ccb763d4f2c49ec8553d00");
    check_sha1_text("http://10.10.9.2:8000/api/monitor/location-a/snapshot",
        "c318064243d0e4e453c9cc978f84046e9a79a494");

    // 标准向量：空串、一个块、56 字节（补位需要第二个块）
    check_sha1("", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check_sha1_text("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    check_sha1_text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // 一百万个 'a'：走很多个完整块
    size_t million = 1000000;
    char *many = malloc(million);
    assert(many != NULL);
    memset(many, 'a', million);
    check_sha1(many, million, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    free(many);
}

static size_t
copy(char *dst, size_t cap, const char *src, size_t len)
{
    return display_text_copy(dst, cap, (const uint8_t *)src, len);
}

static void
test_display_text(void)
{
    char out[64];

    // ASCII 与合法 UTF-8 原样保留
    assert(copy(out, sizeof(out), "tcp://10.10.9.2:3306", 20) == 20);
    assert(strcmp(out, "tcp://10.10.9.2:3306") == 0);
    assert(copy(out, sizeof(out), "数据库", strlen("数据库")) == strlen("数据库"));
    assert(strcmp(out, "数据库") == 0);
    assert(copy(out, sizeof(out), "\xF0\x9F\x98\x80", 4) == 4); // U+1F600
    assert(strcmp(out, "\xF0\x9F\x98\x80") == 0);

    // 控制字符、DEL、NUL 换成 '?'，长度不变
    assert(copy(out, sizeof(out), "a\x1b[2Jb\x7f", 7) == 7);
    assert(strcmp(out, "a?[2Jb?") == 0);
    assert(copy(out, sizeof(out), "a\0b\n", 4) == 4);
    assert(strcmp(out, "a?b?") == 0);

    // C1 控制字符（U+009B 是 8 位的 CSI）
    assert(copy(out, sizeof(out), "x\xC2\x9By", 4) == 4);
    assert(strcmp(out, "x??y") == 0);
    // U+00A0 不是控制字符，保留
    assert(copy(out, sizeof(out), "\xC2\xA0", 2) == 2);
    assert(strcmp(out, "\xC2\xA0") == 0);

    // 非法 UTF-8：孤立的续字节、过长编码、代理区、超过 U+10FFFF、不完整的序列
    assert(copy(out, sizeof(out), "\x80z", 2) == 2 && strcmp(out, "?z") == 0);
    assert(copy(out, sizeof(out), "\xC0\xAF", 2) == 2 && strcmp(out, "??") == 0);
    assert(copy(out, sizeof(out), "\xE0\x80\xAF", 3) == 3 && strcmp(out, "???") == 0);
    assert(copy(out, sizeof(out), "\xED\xA0\x80", 3) == 3 && strcmp(out, "???") == 0);
    assert(copy(out, sizeof(out), "\xF4\x90\x80\x80", 4) == 4 && strcmp(out, "????") == 0);
    assert(copy(out, sizeof(out), "ab\xE6\x95", 4) == 4 && strcmp(out, "ab??") == 0);

    // 容量不够时截断，但不切断多字节字符："数" 占 3 字节，cap 5 只放得下 "a" + "数"
    assert(copy(out, 5, "a数据", strlen("a数据")) == 4);
    assert(strcmp(out, "a数") == 0);
    assert(copy(out, 4, "a数据", strlen("a数据")) == 1);
    assert(strcmp(out, "a") == 0);

    // 边界参数
    assert(copy(out, 1, "abc", 3) == 0 && out[0] == '\0');
    assert(display_text_copy(out, sizeof(out), NULL, 5) == 0 && out[0] == '\0');
    assert(display_text_copy(NULL, sizeof(out), (const uint8_t *)"a", 1) == 0);
}

int
main(void)
{
    test_sha1();
    test_display_text();
    return 0;
}
