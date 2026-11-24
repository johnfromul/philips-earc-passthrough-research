/*
 * Full 52 MiB MI_MAD_ADV_BUF (0x20200000..0x23600000) IEC61937/MAT sync scanner.
 * Also reports unique 64 KiB regions containing IEC61937 syncs (0x4e1ff872).
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

extern int _printk(const char *fmt, ...);
extern void *__ioremap(u64 phys_addr, u64 size, u64 prot);
extern void iounmap(volatile void *addr);
extern u8 arm64_use_ng_mappings;

__attribute__((section(".plt"), used)) void __plt_stub(void) {}
__attribute__((section(".init.plt"), used)) void __init_plt_stub(void) {}
__attribute__((section(".text.ftrace_trampoline"), used)) void __ftrace_trampoline_stub(void) {}
__attribute__((aligned(4096), used)) void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

int init_module(void)
{
    u64 phys_base = 0x20200000ULL, size = 0x03400000ULL;
    u64 prot = 0x006800000000070bULL | (arm64_use_ng_mappings ? 0x800ULL : 0ULL);
    volatile u8 *base = (volatile u8 *)__ioremap(phys_base, size, prot);
    if (!base) return -12;

    const u32 *p = (const u32 *)base;
    u64 count = size / 4;
    u32 hits = 0, logged = 0;
    u64 last_region = ~0ULL;

    for (u64 i = 0; i + 3 < count; i++) {
        u32 w = p[i];
        if (w == 0x4e1ff872U || w == 0xf8724e1fU) {
            u64 phys = phys_base + i * 4;
            u64 region = phys & ~0x1fffffULL; /* 2 MiB bucket */
            hits++;
            if (region != last_region || logged < 6) {
                if (logged < 32) {
                    _printk("\0016[mad_full_scan] HIT#%u phys=0x%llx w0=%08x w1=%08x w2=%08x w3=%08x\n",
                            hits, phys, w, p[i+1], p[i+2], p[i+3]);
                    logged++;
                }
                last_region = region;
            }
        }
    }
    _printk("\0016[mad_full_scan] DONE total_hits=%u (0x2249c000: %08x %08x)\n",
            hits, p[(0x2249c000ULL - phys_base) >> 2], p[((0x2249c000ULL - phys_base) >> 2) + 1]);
    iounmap(base);
    return -11;
}

void cleanup_module(void) {}

struct v670_module { u8 p0[0x18]; char name[56]; u8 p1[0x178-0x50]; int (*init)(void); u8 p2[0x378-0x180]; void (*exit)(void); u8 p3[0x40]; };
__attribute__((section(".gnu.linkonce.this_module"), used)) struct v670_module __this_module = { .name = "mad_full_scan", .init = init_module, .exit = cleanup_module };
struct modversion_info { u64 crc; char name[56]; };
__attribute__((section("__versions"), used, aligned(8))) static const struct modversion_info ____versions[] = {
    { 0xb82e8f7bULL, "module_layout" }, { 0x92997ed8ULL, "_printk" },
    { 0x6b4b2933ULL, "__ioremap" }, { 0xedc03953ULL, "iounmap" }, { 0xaf56600aULL, "arm64_use_ng_mappings" },
};
__attribute__((section(".modinfo"), used, aligned(1))) static const char m1[] = "license=GPL v2";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m2[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m3[] = "name=mad_full_scan";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m4[] = "depends=";
