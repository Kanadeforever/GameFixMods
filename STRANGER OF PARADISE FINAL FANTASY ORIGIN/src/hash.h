#pragma once
#include <cstdint>
#include <cstddef>

// FNV-1 64-bit hash
// 匹配 shader hash: 775de01cde4f0102
#define FNV_64_PRIME 0x100000001b3ULL

inline uint64_t fnv_64_buf(const void *buf, size_t len)
{
    uint64_t hval = 0;
    const unsigned char *bp = (const unsigned char *)buf;
    const unsigned char *be = bp + len;
    while (bp < be) {
        hval *= FNV_64_PRIME;
        hval ^= (uint64_t)*bp++;
    }
    return hval;
}
