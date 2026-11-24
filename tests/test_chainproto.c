/* Device-side chainloader state machine. With argv[1] = a frame file
 * produced by tools/send_kernel.py's build_header() + payload (pytest
 * writes it), it also checks interoperability with the Python sender. */
#include <stdint.h>
#include "test.h"
#include "chainproto.h"

static uint8_t dst[4096];

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static enum cl_event feed(struct cl_rx *st, const uint8_t *b, size_t n, int *hdr_ok)
{
    enum cl_event ev = CL_MORE;
    for (size_t i = 0; i < n; i++) {
        ev = cl_rx_feed(st, b[i]);
        if (ev == CL_HEADER_OK)
            (*hdr_ok)++;
        else if (ev != CL_MORE)
            return ev;
    }
    return ev;
}

static void test_crc32_vectors(void)
{
    CHECK(crc32_update(0, "", 0) == 0);
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u); /* standard check value */
    CHECK(crc32_update(crc32_update(0, "1234", 4), "56789", 5) == 0xCBF43926u);
}

static void test_good_transfer(void)
{
    uint8_t frame[64];
    const char *payload = "kernel-bytes!";
    uint32_t len = (uint32_t)strlen(payload);
    struct cl_rx st;
    int hdr = 0;
    /* line noise and a partial magic before the real header */
    memcpy(frame, "xxOBOBK1", 8);
    put_le32(frame + 8, len);
    put_le32(frame + 12, crc32_update(0, payload, len));
    memcpy(frame + 16, payload, len);
    cl_rx_init(&st, dst, sizeof(dst));
    CHECK(feed(&st, frame, 16 + len, &hdr) == CL_DONE_OK);
    CHECK(hdr == 1);
    CHECK(memcmp(dst, payload, len) == 0);
}

static void test_bad_crc_and_size(void)
{
    uint8_t frame[32];
    struct cl_rx st;
    int hdr = 0;
    memcpy(frame, "OBK1", 4);
    put_le32(frame + 4, 4);
    put_le32(frame + 8, 0x12345678);
    memcpy(frame + 12, "abcd", 4);
    cl_rx_init(&st, dst, sizeof(dst));
    CHECK(feed(&st, frame, 16, &hdr) == CL_DONE_BADCRC);
    CHECK(st.state == 0); /* reset, ready for a retry */

    put_le32(frame + 4, 0);
    cl_rx_init(&st, dst, sizeof(dst));
    CHECK(feed(&st, frame, 12, &hdr) == CL_HEADER_BAD);
    put_le32(frame + 4, sizeof(dst) + 1);
    CHECK(feed(&st, frame, 12, &hdr) == CL_HEADER_BAD);
}

static void test_python_frame(const char *path)
{
    static uint8_t buf[1 << 16];
    FILE *f = fopen(path, "rb");
    size_t n;
    struct cl_rx st;
    int hdr = 0;
    static uint8_t big[1 << 16];
    CHECK(f != NULL);
    if (!f)
        return;
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    cl_rx_init(&st, big, sizeof(big));
    CHECK(feed(&st, buf, n, &hdr) == CL_DONE_OK);
    CHECK(hdr == 1);
    CHECK(st.len == n - 12);
    CHECK(memcmp(big, buf + 12, st.len) == 0);
}

int main(int argc, char **argv)
{
    RUN(test_crc32_vectors);
    RUN(test_good_transfer);
    RUN(test_bad_crc_and_size);
    if (argc > 1)
        test_python_frame(argv[1]);
    TEST_REPORT("test_chainproto");
}
