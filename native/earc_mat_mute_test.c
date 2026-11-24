/*
 * 2-second zero-mute proof test on V670 C9K:
 *   Maps 0x2249C000..0x22640000 (0x1A4000 bytes = 28 MAT frames),
 *   zeros the ring for ~2 seconds while HDMI Dolby Atmos is playing,
 *   unmaps, and auto-unloads (-EAGAIN).
 *   If the soundbar mutes/drops Atmos for exactly those ~2s and resumes cleanly,
 *   0x2249C000 is proven to be the live SE DSP -> eARC TX output ring.
 */

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

extern int _printk(const char *fmt, ...);
extern void *__ioremap(u64 phys_addr, u64 size, u64 prot);
extern void iounmap(volatile void *addr);
extern void msleep(unsigned int msecs);
extern u8 arm64_use_ng_mappings;

__attribute__((section(".plt"), used)) void __plt_stub(void) {}
__attribute__((section(".init.plt"), used)) void __init_plt_stub(void) {}
__attribute__((section(".text.ftrace_trampoline"), used)) void __ftrace_trampoline_stub(void) {}

__attribute__((aligned(4096), used))
void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

#define RING_PHYS 0x2249C000ULL
#define RING_SIZE 0x001A4000ULL

int init_module(void)
{
    u64 prot = 0x006800000000070bULL;
    if (arm64_use_ng_mappings)
        prot |= 0x800ULL;

    volatile u64 *ring = (volatile u64 *)__ioremap(RING_PHYS, RING_SIZE, prot);
    if (!ring) {
        _printk("\0016[earc_mat_mute] ioremap failed\n");
        return -12;
    }

    const u32 *r32 = (const u32 *)ring;
    u32 hits = 0, first_off = 0xffffffffU;
    for (u64 i = 0; i < RING_SIZE / 4; i++) {
        if (r32[i] == 0x4e1ff872U) {
            if (!hits) first_off = (u32)(i * 4);
            hits++;
        }
    }
    _printk("\0016[earc_mat_mute] START: %u MAT syncs (first_off=0x%x). Zeroing ring for ~2s...\n",
            hits, first_off);

    for (u32 iter = 0; iter < 150; iter++) {
        for (u64 i = 0; i < RING_SIZE / 8; i++)
            ring[i] = 0ULL;
        __asm__ volatile("dmb ishst" ::: "memory");
        msleep(8);
    }

    _printk("\0016[earc_mat_mute] DONE: ~2s zeroing finished, auto-unloading.\n");
    iounmap(ring);
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
    .name = "earc_mat_mute_test",
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
    { 0xf9a482f9ULL, "msleep" },
};

__attribute__((section(".modinfo"), used, aligned(1))) static const char m1[] = "license=GPL v2";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m2[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m3[] = "name=earc_mat_mute_test";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m4[] = "depends=";
