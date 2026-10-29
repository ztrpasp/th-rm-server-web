#ifndef _RM_DISPLAY_TEXT_H_
#define _RM_DISPLAY_TEXT_H_

#include <stddef.h>
#include <stdint.h>

/*
 * 把来自报文、不可信的字节串转成可以安全展示的文本，写入 dst（容量 cap，含结尾 NUL）：
 * - 合法的 UTF-8 原样保留；
 * - 非法的 UTF-8 字节、C0 控制字符（含 NUL）、DEL、C1 控制字符逐字节换成 '?'。
 * 输出与输入逐字节对应，长度不变（最多 cap - 1），不会在多字节字符中间截断。
 * 结果里没有控制字符，打印到终端不会被当成转义序列；写进 JSON 只需转义 '"' 和 '\'。
 * 返回写入的字节数（不含 NUL）。
 */
size_t display_text_copy(char *dst, size_t cap, const uint8_t *src, size_t len);

#endif
