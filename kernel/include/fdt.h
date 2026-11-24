/* Minimal read-only Flattened Device Tree (DTB v17) parser.
 *
 * Node handles are byte offsets of FDT_BEGIN_NODE tokens inside the
 * structure block; the root node is always offset 0. Everything is
 * bounds-checked against the header's totalsize, and all accesses are
 * 4-byte aligned big-endian loads, so it is safe to run with the MMU off. */
#ifndef ORBIT_FDT_H
#define ORBIT_FDT_H

#include <stdint.h>

#define FDT_MAGIC 0xd00dfeedu

int         fdt_check(const void *fdt);             /* 0 if a sane v16+ blob */
uint32_t    fdt_totalsize(const void *fdt);
const char *fdt_node_name(const void *fdt, int node);
int         fdt_subnode(const void *fdt, int parent, const char *name);
/* "/", "/memory", "/soc/serial@7d001000". A component without '@' also
 * matches a node name that has a unit address ("memory" ~ "memory@0"). */
int         fdt_path_offset(const void *fdt, const char *path);
/* Next node after `start` (pass -1 to begin) whose "compatible" list
 * contains `compat`. */
int         fdt_node_by_compatible(const void *fdt, int start, const char *compat);
const void *fdt_getprop(const void *fdt, int node, const char *name, int *len);
int         fdt_parent(const void *fdt, int node);
uint32_t    fdt_prop_u32(const void *fdt, int node, const char *name, uint32_t dflt);
int         fdt_address_cells(const void *fdt, int node); /* default 2 */
int         fdt_size_cells(const void *fdt, int node);    /* default 1 */
/* idx-th (addr, size) pair of the node's "reg", translated through parent
 * "ranges" up to the root (CPU physical address). 0 on success. */
int         fdt_reg(const void *fdt, int node, int idx, uint64_t *addr, uint64_t *size);
int         fdt_is_enabled(const void *fdt, int node); /* status absent/"okay" */
/* Resolve an alias name (e.g. "serial0") or absolute path to a node. */
int         fdt_resolve(const void *fdt, const char *path_or_alias, int len);
/* n-th /memreserve/ entry; returns 0 on success, -1 past the end. */
int         fdt_memreserve(const void *fdt, int n, uint64_t *addr, uint64_t *size);

#endif
