// SHA-1，按 FIPS 180-4 写的最小实现，只做一次性哈希（不需要流式接口）
#include "vendor/sha1.h"

#include <string.h>

static uint32_t
rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void
sha1_block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 |
            (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    for (i = 16; i < 80; i++)
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = h[0];
    b = h[1];
    c = h[2];
    d = h[3];
    e = h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;

        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        t = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void
rm_sha1(const void *data, size_t len, uint8_t digest[RM_SHA1_DIGEST_LEN])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    const uint8_t *p = (const uint8_t *)data;
    size_t left = len;
    uint8_t tail[128];
    size_t tail_len;
    uint64_t bits = (uint64_t)len * 8;
    int i;

    while (left >= 64) {
        sha1_block(h, p);
        p += 64;
        left -= 64;
    }

    // 补位：剩余数据 + 0x80 + 若干 0 + 8 字节大端位长，凑成 1 或 2 个块
    memset(tail, 0, sizeof(tail));
    if (left != 0)
        memcpy(tail, p, left);
    tail[left] = 0x80;
    tail_len = left + 1 + 8 <= 64 ? 64 : 128;
    for (i = 0; i < 8; i++)
        tail[tail_len - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha1_block(h, tail);
    if (tail_len == 128)
        sha1_block(h, tail + 64);

    for (i = 0; i < 5; i++) {
        digest[4 * i] = (uint8_t)(h[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(h[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(h[i] >> 8);
        digest[4 * i + 3] = (uint8_t)h[i];
    }
}
