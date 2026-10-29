#include "vendor/display_text.h"

#include <stdbool.h>

static bool
is_cont(uint8_t c)
{
    return (c & 0xC0) == 0x80;
}

/*
 * 从 s[0] 开始的合法 UTF-8 字符占几个字节（剩余 left 字节）；不合法返回 0。
 * 按 RFC 3629：拒绝过长编码、代理区（U+D800–U+DFFF）和超过 U+10FFFF 的码点。
 */
static size_t
utf8_char_len(const uint8_t *s, size_t left)
{
    uint8_t c = s[0];

    if (c < 0x80)
        return 1;
    if (c >= 0xC2 && c <= 0xDF)
        return left >= 2 && is_cont(s[1]) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        uint8_t lo = c == 0xE0 ? 0xA0 : 0x80;
        uint8_t hi = c == 0xED ? 0x9F : 0xBF;
        return left >= 3 && s[1] >= lo && s[1] <= hi && is_cont(s[2]) ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        uint8_t lo = c == 0xF0 ? 0x90 : 0x80;
        uint8_t hi = c == 0xF4 ? 0x8F : 0xBF;
        return left >= 4 && s[1] >= lo && s[1] <= hi && is_cont(s[2]) &&
            is_cont(s[3]) ? 4 : 0;
    }
    return 0;
}

size_t
display_text_copy(char *dst, size_t cap, const uint8_t *src, size_t len)
{
    size_t in = 0, out = 0;

    if (dst == NULL || cap == 0)
        return 0;
    if (src == NULL)
        len = 0;

    while (in < len) {
        size_t n = utf8_char_len(src + in, len - in);
        size_t i;
        bool printable;

        if (n == 0) {
            // 非法字节：只替换这一个字节，从下一个字节重新同步
            n = 1;
            printable = false;
        } else if (n == 1) {
            printable = src[in] >= 0x20 && src[in] != 0x7F;
        } else {
            // C1 控制字符 U+0080–U+009F 编码为 C2 80 – C2 9F
            printable = !(n == 2 && src[in] == 0xC2 && src[in + 1] <= 0x9F);
        }

        if (out + n > cap - 1)
            break;
        for (i = 0; i < n; i++)
            dst[out + i] = printable ? (char)src[in + i] : '?';
        out += n;
        in += n;
    }
    dst[out] = '\0';
    return out;
}
