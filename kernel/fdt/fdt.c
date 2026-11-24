#include "fdt.h"
#include "kstring.h"

#define TOK_BEGIN 1u
#define TOK_END   2u
#define TOK_PROP  3u
#define TOK_NOP   4u
#define TOK_DONE  9u

#define MAX_DEPTH 32

struct hdr {
    uint32_t magic, totalsize, off_struct, off_strings, off_rsvmap, version,
        last_comp, boot_cpuid, size_strings, size_struct;
};

static inline uint32_t be32(const void *p)
{
    const uint8_t *b = p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}

static uint32_t hdr_field(const void *fdt, int idx)
{
    return be32((const uint8_t *)fdt + 4 * idx);
}

#define H_TOTAL(f)   hdr_field(f, 1)
#define H_STRUCT(f)  hdr_field(f, 2)
#define H_STRINGS(f) hdr_field(f, 3)
#define H_RSV(f)     hdr_field(f, 4)
#define H_VERSION(f) hdr_field(f, 5)
#define H_SZSTR(f)   hdr_field(f, 8)
#define H_SZSTRUCT(f) hdr_field(f, 9)

int fdt_check(const void *fdt)
{
    uint32_t total;
    if (!fdt || ((uintptr_t)fdt & 3))
        return -1;
    if (be32(fdt) != FDT_MAGIC)
        return -1;
    total = H_TOTAL(fdt);
    if (total < 40 || total > (64u << 20))
        return -1;
    if (H_VERSION(fdt) < 16)
        return -1;
    if (H_STRUCT(fdt) + H_SZSTRUCT(fdt) > total ||
        H_STRINGS(fdt) + H_SZSTR(fdt) > total || (H_STRUCT(fdt) & 3))
        return -1;
    return 0;
}

uint32_t fdt_totalsize(const void *fdt)
{
    return H_TOTAL(fdt);
}

static const uint8_t *sblock(const void *fdt)
{
    return (const uint8_t *)fdt + H_STRUCT(fdt);
}

static uint32_t tok_at(const void *fdt, int off)
{
    if (off < 0 || (uint32_t)off + 4 > H_SZSTRUCT(fdt))
        return TOK_DONE;
    return be32(sblock(fdt) + off);
}

static inline int align4(int x)
{
    return (x + 3) & ~3;
}

/* Offset of the token following the one at `off`. -1 on malformed data. */
static int next_tok(const void *fdt, int off)
{
    uint32_t t = tok_at(fdt, off);
    const uint8_t *s = sblock(fdt);
    uint32_t lim = H_SZSTRUCT(fdt);
    switch (t) {
    case TOK_BEGIN: {
        uint32_t i = (uint32_t)off + 4;
        while (i < lim && s[i])
            i++;
        if (i >= lim)
            return -1;
        return align4((int)i + 1);
    }
    case TOK_PROP: {
        uint32_t len;
        if ((uint32_t)off + 12 > lim)
            return -1;
        len = be32(s + off + 4);
        if ((uint32_t)off + 12 + len > lim)
            return -1;
        return align4(off + 12 + (int)len);
    }
    case TOK_END:
    case TOK_NOP:
        return off + 4;
    default:
        return -1;
    }
}

const char *fdt_node_name(const void *fdt, int node)
{
    if (tok_at(fdt, node) != TOK_BEGIN)
        return 0;
    return (const char *)sblock(fdt) + node + 4;
}

/* Offset just past the END token that closes `node`. */
static int skip_node(const void *fdt, int node)
{
    int depth = 0, off = node;
    for (;;) {
        uint32_t t = tok_at(fdt, off);
        if (t == TOK_BEGIN)
            depth++;
        else if (t == TOK_END && --depth == 0)
            return off + 4;
        else if (t == TOK_DONE)
            return -1;
        off = next_tok(fdt, off);
        if (off < 0)
            return -1;
    }
}

/* Name match: exact, or `want` has no '@' and equals the part before '@'. */
static int name_matches(const char *have, const char *want, int wlen)
{
    int i;
    for (i = 0; i < wlen; i++)
        if (have[i] != want[i])
            return 0;
    if (have[wlen] == 0)
        return 1;
    if (have[wlen] == '@') {
        for (i = 0; i < wlen; i++)
            if (want[i] == '@')
                return 0;
        return 1;
    }
    return 0;
}

static int subnode_n(const void *fdt, int parent, const char *name, int len)
{
    int off;
    if (tok_at(fdt, parent) != TOK_BEGIN)
        return -1;
    off = next_tok(fdt, parent);
    while (off >= 0) {
        uint32_t t = tok_at(fdt, off);
        if (t == TOK_BEGIN) {
            if (name_matches(fdt_node_name(fdt, off), name, len))
                return off;
            off = skip_node(fdt, off);
        } else if (t == TOK_PROP || t == TOK_NOP) {
            off = next_tok(fdt, off);
        } else {
            return -1; /* END of parent or DONE */
        }
    }
    return -1;
}

int fdt_subnode(const void *fdt, int parent, const char *name)
{
    return subnode_n(fdt, parent, name, (int)k_strlen(name));
}

static int path_offset_n(const void *fdt, const char *path, int plen)
{
    int node = 0, i = 0;
    if (plen <= 0 || path[0] != '/')
        return -1;
    while (i < plen) {
        int start, len;
        while (i < plen && path[i] == '/')
            i++;
        if (i >= plen)
            break;
        start = i;
        while (i < plen && path[i] != '/')
            i++;
        len = i - start;
        node = subnode_n(fdt, node, path + start, len);
        if (node < 0)
            return -1;
    }
    return node;
}

int fdt_path_offset(const void *fdt, const char *path)
{
    return path_offset_n(fdt, path, (int)k_strlen(path));
}

const void *fdt_getprop(const void *fdt, int node, const char *name, int *lenp)
{
    int off;
    const uint8_t *s = sblock(fdt);
    const char *strs = (const char *)fdt + H_STRINGS(fdt);
    uint32_t szstr = H_SZSTR(fdt);
    if (tok_at(fdt, node) != TOK_BEGIN)
        return 0;
    off = next_tok(fdt, node);
    while (off >= 0) {
        uint32_t t = tok_at(fdt, off);
        if (t == TOK_PROP) {
            uint32_t len = be32(s + off + 4), nameoff = be32(s + off + 8);
            if (nameoff < szstr && k_strcmp(strs + nameoff, name) == 0) {
                if (lenp)
                    *lenp = (int)len;
                return s + off + 12;
            }
        } else if (t != TOK_NOP) {
            break; /* properties precede subnodes */
        }
        off = next_tok(fdt, off);
    }
    return 0;
}

int fdt_node_by_compatible(const void *fdt, int start, const char *compat)
{
    int off = 0, clen = (int)k_strlen(compat);
    if (start >= 0) {
        off = next_tok(fdt, start);
    }
    while (off >= 0) {
        uint32_t t = tok_at(fdt, off);
        if (t == TOK_DONE)
            return -1;
        if (t == TOK_BEGIN) {
            int len, i = 0;
            const char *p = fdt_getprop(fdt, off, "compatible", &len);
            while (p && i < len) {
                int sl = (int)k_strnlen(p + i, (size_t)(len - i));
                if (sl == clen && k_memcmp(p + i, compat, (size_t)clen) == 0)
                    return off;
                i += sl + 1;
            }
        }
        off = next_tok(fdt, off);
    }
    return -1;
}

int fdt_parent(const void *fdt, int node)
{
    int stack[MAX_DEPTH];
    int depth = 0, off = 0;
    if (node <= 0)
        return -1;
    while (off >= 0) {
        uint32_t t = tok_at(fdt, off);
        if (t == TOK_BEGIN) {
            if (off == node)
                return depth > 0 ? stack[depth - 1] : -1;
            if (depth >= MAX_DEPTH)
                return -1;
            stack[depth++] = off;
        } else if (t == TOK_END) {
            if (depth == 0)
                return -1;
            depth--;
        } else if (t == TOK_DONE) {
            return -1;
        }
        off = next_tok(fdt, off);
    }
    return -1;
}

uint32_t fdt_prop_u32(const void *fdt, int node, const char *name, uint32_t dflt)
{
    int len;
    const void *p = fdt_getprop(fdt, node, name, &len);
    if (!p || len < 4)
        return dflt;
    return be32(p);
}

int fdt_address_cells(const void *fdt, int node)
{
    return (int)fdt_prop_u32(fdt, node, "#address-cells", 2);
}

int fdt_size_cells(const void *fdt, int node)
{
    return (int)fdt_prop_u32(fdt, node, "#size-cells", 1);
}

static uint64_t read_cells(const uint8_t *p, int n)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < n; i++)
        v = (v << 32) | be32(p + 4 * i);
    return v;
}

int fdt_reg(const void *fdt, int node, int idx, uint64_t *addr, uint64_t *size)
{
    int parent = fdt_parent(fdt, node);
    int ac, sc, len, entry;
    const uint8_t *reg;
    uint64_t a, s;
    if (parent < 0)
        return -1;
    ac = fdt_address_cells(fdt, parent);
    sc = fdt_size_cells(fdt, parent);
    if (ac < 1 || ac > 2 || sc < 0 || sc > 2)
        return -1;
    reg = fdt_getprop(fdt, node, "reg", &len);
    entry = 4 * (ac + sc);
    if (!reg || len < entry * (idx + 1))
        return -1;
    a = read_cells(reg + entry * idx, ac);
    s = read_cells(reg + entry * idx + 4 * ac, sc);

    /* Walk up through bus nodes applying "ranges". */
    {
        int bus = parent;
        while (bus > 0) {
            int busp = fdt_parent(fdt, bus);
            int cac, pac, csc, rlen, rent, i;
            const uint8_t *r;
            if (busp < 0)
                return -1;
            r = fdt_getprop(fdt, bus, "ranges", &rlen);
            if (!r)
                return -1; /* no ranges: not translatable to CPU space */
            if (rlen == 0) {
                bus = busp; /* empty ranges: identity */
                continue;
            }
            cac = fdt_address_cells(fdt, bus);
            csc = fdt_size_cells(fdt, bus);
            pac = fdt_address_cells(fdt, busp);
            if (cac < 1 || cac > 2 || pac < 1 || pac > 2 || csc < 1 || csc > 2)
                return -1;
            rent = 4 * (cac + pac + csc);
            for (i = 0; i + rent <= rlen; i += rent) {
                uint64_t cb = read_cells(r + i, cac);
                uint64_t pb = read_cells(r + i + 4 * cac, pac);
                uint64_t rs = read_cells(r + i + 4 * (cac + pac), csc);
                if (a >= cb && a - cb < rs) {
                    a = a - cb + pb;
                    break;
                }
            }
            if (i + rent > rlen)
                return -1; /* no window covers the address */
            bus = busp;
        }
    }
    if (addr)
        *addr = a;
    if (size)
        *size = s;
    return 0;
}

int fdt_is_enabled(const void *fdt, int node)
{
    int len;
    const char *st = fdt_getprop(fdt, node, "status", &len);
    if (!st)
        return 1;
    return k_strncmp(st, "okay", 4) == 0 || k_strncmp(st, "ok", 3) == 0;
}

int fdt_resolve(const void *fdt, const char *p, int len)
{
    int aliases, alen;
    const char *target;
    if (len <= 0)
        return -1;
    if (p[0] == '/')
        return path_offset_n(fdt, p, len);
    aliases = fdt_path_offset(fdt, "/aliases");
    if (aliases < 0)
        return -1;
    {
        /* Look the alias up by exact name (needs a NUL-terminated copy). */
        char name[64];
        if (len >= (int)sizeof(name))
            return -1;
        k_memcpy(name, p, (size_t)len);
        name[len] = 0;
        target = fdt_getprop(fdt, aliases, name, &alen);
    }
    if (!target || alen < 2 || target[0] != '/')
        return -1;
    return path_offset_n(fdt, target, (int)k_strnlen(target, (size_t)alen));
}

int fdt_memreserve(const void *fdt, int n, uint64_t *addr, uint64_t *size)
{
    const uint8_t *r = (const uint8_t *)fdt + H_RSV(fdt);
    int i;
    for (i = 0;; i++) {
        uint64_t a, s;
        if (H_RSV(fdt) + 16u * (uint32_t)(i + 1) > H_TOTAL(fdt))
            return -1;
        a = read_cells(r + 16 * i, 2);
        s = read_cells(r + 16 * i + 8, 2);
        if (a == 0 && s == 0)
            return -1;
        if (i == n) {
            *addr = a;
            *size = s;
            return 0;
        }
    }
}
