/*
 * Read-only MAT ring boundary & structure inspector for V670 C9K.
 * Inspects phys 0x22490000..0x22680000 (around SE_MAD_BASE + 0x98C000 = 0x2249C000)
 * to determine exact ring start, ring end, wraparound offset, and frame layout.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

extern int _printk(const char *fmt, ...);
extern void *__ioremap(u64 phys_addr, u64 size, u64 prot);
extern void iounmap(volatile void *addr);
extern u8 arm64_use_ng_mappings;

__attribute__((section(".plt"), used)) void __plt_stub(void) {}
__attribute__((section(".init.plt"), used)) void __init_plt_stub(void) {}
__attribute__((section(".text.ftrace_trampoline"), used)) void __ftrace_trampoline_stub(void) {}

__attribute__((aligned(4096), used))
void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

int init_module(void)
{
    u64 phys_base = 0x22490000ULL;
    u64 size = 0x001f0000ULL; /* 0x22490000..0x22680000 */
    u64 prot = 0x006800000000070bULL;
    if (arm64_use_ng_mappings)
        prot |= 0x800ULL;

    volatile u8 *base = (volatile u8 *)__ioremap(phys_base, size, prot);
    if (!base) {
        _printk("\0016[mad_mat_scan] ioremap failed\n");
        return -12;
    }

    const u32 *p = (const u32 *)base;

    /* 1. Check 2 KiB page activity before HIT#0 (0x22490000..0x224a4000) */
    _printk("\0016[mad_mat_scan] === PRE-HIT0 (0x22490000..0x224a4000) ===\n");
    for (u64 off = 0; off < 0x14000; off += 0x800) {
        u32 nz = 0;
        for (u64 j = 0; j < 0x200; j++)
            if (p[(off >> 2) + j] != 0) nz++;
        if (nz) {
            u32 i = (u32)(off >> 2);
            _printk("\0016[mad_mat_scan] phys=0x%llx nz=%u/512 w0=%08x w1=%08x w2=%08x w3=%08x\n",
                    phys_base + off, nz, p[i], p[i+1], p[i+2], p[i+3]);
        }
    }

    /* 2. Check 2 KiB page activity around & after HIT#27 (0x22638000..0x22680000) */
    _printk("\0016[mad_mat_scan] === POST-HIT27 (0x22638000..0x22680000) ===\n");
    for (u64 off = 0x1a8000; off < size; off += 0x800) {
        u32 nz = 0;
        for (u64 j = 0; j < 0x200; j++)
            if (p[(off >> 2) + j] != 0) nz++;
        if (nz) {
            u32 i = (u32)(off >> 2);
            _printk("\0016[mad_mat_scan] phys=0x%llx nz=%u/512 w0=%08x w1=%08x w2=%08x w3=%08x\n",
                    phys_base + off, nz, p[i], p[i+1], p[i+2], p[i+3]);
        }
    }

    /* 3. Inspect first 16 words of HIT#0 (0x224a3800) and HIT#1 (0x224b2800) to see MAT payload structure */
    u32 h0 = (u32)((0x224a3800ULL - phys_base) >> 2);
    _printk("\0016[mad_mat_scan] HIT#0@0x224a3800: %08x %08x %08x %08x %08x %08x %08x %08x\n",
            p[h0], p[h0+1], p[h0+2], p[h0+3], p[h0+4], p[h0+5], p[h0+6], p[h0+7]);

    iounmap(base);
    return -11; /* -EAGAIN */
}

void cleanup_module(void) {}

struct v670_module {
    u8 pad0[0x18];
    char name[56];
    u8 pad1[0x178 - 0x18 - 56];
    int (*init)(void);
    u8 pad2[0x378 - 0x178 - 8];
    void (*exit)(void);
    u8 pad3[0x3c0 - 0x378 - 8];
};

__attribute__((section(".gnu.linkonce.this_module"), used))
struct v670_module __this_module = {
    .name = "mad_mat_scan",
    .init = init_module,
    .exit = cleanup_module,
};

struct modversion_info { u64 crc; char name[56]; };

__attribute__((section("__versions"), used, aligned(8)))
static const struct modversion_info ____versions[] = {
    { 0xb82e8f7bULL, "module_layout" },
    { 0x92997ed8ULL, "_printk" },
    { 0x6b4b2933ULL, "__ioremap" },
    { 0xedc03953ULL, "iounmap" },
    { 0xaf56600aULL, "arm64_use_ng_mappings" },
};

__attribute__((section(".modinfo"), used, aligned(1))) static const char m1[] = "license=GPL v2";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m2[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m3[] = "name=mad_mat_scan";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m4[] = "depends=";
