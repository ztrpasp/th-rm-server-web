#ifndef _RM_SHA1_H_
#define _RM_SHA1_H_

#include <stddef.h>
#include <stdint.h>

#define RM_SHA1_DIGEST_LEN 20

// 计算 data 的 SHA-1（FIPS 180-4）。用来校验 ANN 里的 SID 名称：SHA1(名称) == L_SID
void rm_sha1(const void *data, size_t len, uint8_t digest[RM_SHA1_DIGEST_LEN]);

#endif
