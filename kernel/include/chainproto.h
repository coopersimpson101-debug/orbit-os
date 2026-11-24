/* Serial chainloader wire protocol (device side), shared by the
 * chainloader and the host unit tests. See docs/chainloader-protocol.md.
 *
 *   device -> host : banner text, then 0x03 0x03 0x03   ("send me a kernel")
 *   host -> device : "OBK1" | u32 length (LE) | u32 CRC-32 (LE)
 *   device -> host : "OK" (header accepted) or "SZ" (bad length; restart)
 *   host -> device : <length> payload bytes
 *   device -> host : "OK" (CRC matches, jumping) or "CK" (mismatch; restart)
 */
#ifndef ORBIT_CHAINPROTO_H
#define ORBIT_CHAINPROTO_H

#include <stddef.h>
#include <stdint.h>

#define CL_MAGIC "OBK1"

enum cl_event {
    CL_MORE = 0,       /* keep feeding */
    CL_HEADER_OK,      /* reply "OK" */
    CL_HEADER_BAD,     /* reply "SZ"; state machine reset */
    CL_DONE_OK,        /* payload complete and CRC good: reply "OK" and boot */
    CL_DONE_BADCRC,    /* reply "CK"; state machine reset */
};

struct cl_rx {
    uint8_t *dst;
    uint32_t max;
    int state;         /* 0 magic, 1 header, 2 payload */
    uint32_t matched;  /* magic bytes matched */
    uint8_t hdr[8];
    uint32_t hdr_n;
    uint32_t len, crc, got, running_crc;
};

uint32_t crc32_update(uint32_t crc, const void *data, size_t n); /* start with 0 */
void     cl_rx_init(struct cl_rx *st, uint8_t *dst, uint32_t max);
enum cl_event cl_rx_feed(struct cl_rx *st, uint8_t byte);

#endif
