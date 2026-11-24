/*
 * Controlled PoC for V670 C9K:
 *   Phase 1 (insmod): Snapshots the 28 real HDMI Dolby Atmos MAT frames currently
 *                     resident in the SE DSP Non-PCM DDR ring (0x2249C000, size 0x1A4000)
 *                     into vmalloc RAM and stays loaded.
 *   Phase 2 (rmmod):  Logs the current contents of 0x2249C000 (e.g. during internal
 *                     MM_IN TRUEHD_RAW playback), then overwrites the ring with the
 *                     saved Dolby Atmos MAT frames for ~5 seconds so SE DSP / eARC TX
 *                     streams them to the soundbar, then frees memory and unloads.
 */

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

extern int _printk(const char *fmt, ...);
extern void *__ioremap(u64 phys_addr, u64 size, u64 prot);
extern void iounmap(volatile void *addr);
extern void *vmalloc(u64 size);
extern void vfree(const void *addr);
extern void msleep(unsigned int msecs);
extern u8 arm64_use_ng_mappings;

__attribute__((section(".plt"), used)) void __plt_stub(void) {}
__attribute__((section(".init.plt"), used)) void __init_plt_stub(void) {}
__attribute__((section(".text.ftrace_trampoline"), used)) void __ftrace_trampoline_stub(void) {}

__attribute__((aligned(4096), used))
void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

#define RING_PHYS 0x2249C000ULL
#define RING_SIZE 0x001A4000ULL /* 28 * 61440 bytes */

static u64 *saved_ring;
static u32 saved_hits;

static void *map_ring(void)
{
    u64 prot = 0x006800000000070bULL;
    if (arm64_use_ng_mappings)
        prot |= 0x800ULL;
    return __ioremap(RING_PHYS, RING_SIZE, prot);
}

static u32 count_mat_sync(const u32 *p, u64 words, u32 *first_off)
{
    u32 hits = 0;
    *first_off = 0xffffffffU;
    for (u64 i = 0; i < words; i++) {
        if (p[i] == 0x4e1ff872U) {
            if (hits == 0)
                *first_off = (u32)(i * 4);
            hits++;
        }
    }
    return hits;
}

int init_module(void)
{
    volatile u64 *ring = (volatile u64 *)map_ring();
    if (!ring) {
        _printk("\0016[earc_mat_poc] ioremap failed\n");
        return -12;
    }

    u32 first_off = 0;
    saved_hits = count_mat_sync((const u32 *)ring, RING_SIZE / 4, &first_off);
    _printk("\0016[earc_mat_poc] Phase 1: found %u MAT syncs (first_off=0x%x)\n",
            saved_hits, first_off);

    if (saved_hits == 0) {
        _printk("\0016[earc_mat_poc] ABORT: no MAT frames in ring to snapshot\n");
        iounmap(ring);
        return -22; /* -EINVAL */
    }

    saved_ring = (u64 *)vmalloc(RING_SIZE);
    if (!saved_ring) {
        _printk("\0016[earc_mat_poc] vmalloc failed\n");
        iounmap(ring);
        return -12;
    }

    for (u64 i = 0; i < RING_SIZE / 8; i++)
        saved_ring[i] = ring[i];

    iounmap(ring);
    _printk("\0016[earc_mat_poc] Phase 1 DONE: snapshotted 0x%llx bytes (%u MAT frames). Start internal playback, then rmmod earc_mat_poc!\n",
            RING_SIZE, saved_hits);
    return 0;
}

void cleanup_module(void)
{
    if (!saved_ring)
        return;

    volatile u64 *ring = (volatile u64 *)map_ring();
    if (!ring) {
        _printk("\0016[earc_mat_poc] Phase 2: ioremap failed\n");
        vfree(saved_ring);
        saved_ring = 0;
        return;
    }

    u32 first_off = 0;
    u32 pre_hits = count_mat_sync((const u32 *)ring, RING_SIZE / 4, &first_off);
    const u32 *r32 = (const u32 *)ring;
    _printk("\0016[earc_mat_poc] Phase 2 BEFORE inject: %u MAT syncs (first_off=0x%x) w0=%08x w1=%08x\n",
            pre_hits, first_off, r32[0], r32[1]);

    _printk("\0016[earc_mat_poc] Phase 2 INJECTING %u saved Atmos MAT frames for ~5s...\n", saved_hits);
    for (u32 iter = 0; iter < 500; iter++) {
        for (u64 i = 0; i < RING_SIZE / 8; i++)
            ring[i] = saved_ring[i];
        __asm__ volatile("dmb ishst" ::: "memory");
        msleep(8);
    }

    _printk("\0016[earc_mat_poc] Phase 2 DONE: injection stopped, unloading cleanly.\n");
    iounmap(ring);
    vfree(saved_ring);
    saved_ring = 0;
}

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
    .name = "earc_mat_poc",
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
    { 0xd6ee688fULL, "vmalloc" },
    { 0x999e8297ULL, "vfree" },
    { 0xf9a482f9ULL, "msleep" },
};

__attribute__((section(".modinfo"), used, aligned(1))) static const char m1[] = "license=GPL v2";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m2[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m3[] = "name=earc_mat_poc";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m4[] = "depends=";
