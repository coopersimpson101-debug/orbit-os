#include "chainproto.h"

/* Bitwise CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) - identical to
 * Python's zlib.crc32. Table-free to keep the chainloader tiny. */
uint32_t crc32_update(uint32_t crc, const void *data, size_t n)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (n--) {
        int k;
        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void cl_rx_init(struct cl_rx *st, uint8_t *dst, uint32_t max)
{
    st->dst = dst;
    st->max = max;
    st->state = 0;
    st->matched = 0;
    st->hdr_n = 0;
    st->len = st->crc = st->got = st->running_crc = 0;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

enum cl_event cl_rx_feed(struct cl_rx *st, uint8_t b)
{
    static const char magic[] = CL_MAGIC;
    switch (st->state) {
    case 0:
        if (b == (uint8_t)magic[st->matched]) {
            if (++st->matched == 4) {
                st->state = 1;
                st->hdr_n = 0;
            }
        } else {
            st->matched = (b == (uint8_t)magic[0]) ? 1 : 0;
        }
        return CL_MORE;
    case 1:
        st->hdr[st->hdr_n++] = b;
        if (st->hdr_n < 8)
            return CL_MORE;
        st->len = le32(st->hdr);
        st->crc = le32(st->hdr + 4);
        if (st->len == 0 || st->len > st->max) {
            cl_rx_init(st, st->dst, st->max);
            return CL_HEADER_BAD;
        }
        st->got = 0;
        st->running_crc = 0;
        st->state = 2;
        return CL_HEADER_OK;
    case 2:
        st->dst[st->got++] = b;
        if (st->got < st->len)
            return CL_MORE;
        st->running_crc = crc32_update(0, st->dst, st->len);
        if (st->running_crc == st->crc)
            return CL_DONE_OK;
        cl_rx_init(st, st->dst, st->max);
        return CL_DONE_BADCRC;
    }
    return CL_MORE;
}
