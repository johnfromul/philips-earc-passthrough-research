typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

extern int _printk(const char *fmt, ...);
extern void *__ioremap(u64 phys_addr, u64 size, u64 prot);
extern void iounmap(volatile void *addr);
extern void usleep_range_state(unsigned long min, unsigned long max, unsigned int state);
extern u8 arm64_use_ng_mappings;

__attribute__((section(".plt"), used)) void __plt_stub(void) {}
__attribute__((section(".init.plt"), used)) void __init_plt_stub(void) {}
__attribute__((section(".text.ftrace_trampoline"), used)) void __ftrace_trampoline_stub(void) {}

__attribute__((aligned(4096), used))
void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

#define RING_PHYS 0x2249C000ULL
#define RING_SIZE 0x001A4000ULL
#define SLOT_SIZE 0x0000F000ULL
#define NUM_SLOTS 28U
#define SENTINEL  0xCAFEBABEU

static inline u32 wrap(u32 off) { return off % (u32)RING_SIZE; }

int init_module(void)
{
    u64 prot = 0x006800000000070bULL;
    if (arm64_use_ng_mappings)
        prot |= 0x800ULL;

    volatile u8 *ring = (volatile u8 *)__ioremap(RING_PHYS, RING_SIZE, prot);
    if (!ring) return -12;

    u32 base_off = 0xffffffffU;
    for (u32 off = 0; off < (u32)SLOT_SIZE; off += 4U) {
        if (*(volatile u32 *)(ring + off) == 0x4e1ff872U) {
            base_off = off;
            break;
        }
    }
    if (base_off == 0xffffffffU) { iounmap(ring); return -11; }

    /* Find which slot is written right now */
    for (u32 s = 0; s < NUM_SLOTS; s++)
        *(volatile u32 *)(ring + wrap(base_off + s * (u32)SLOT_SIZE + (u32)SLOT_SIZE - 4U)) = SENTINEL;
    __asm__ volatile("dmb ishst" ::: "memory");

    u32 target = 0xffffffffU;
    for (u32 t = 0; t < 200; t++) {
        usleep_range_state(200, 300, 2);
        __asm__ volatile("dmb ishld" ::: "memory");
        for (u32 s = 0; s < NUM_SLOTS; s++) {
            if (*(volatile u32 *)(ring + wrap(base_off + s * (u32)SLOT_SIZE + (u32)SLOT_SIZE - 4U)) != SENTINEL) {
                target = (s + 2U) % NUM_SLOTS; /* watch 2 slots ahead */
                break;
            }
        }
        if (target != 0xffffffffU) break;
    }

    /* Clean all sentinels */
    for (u32 s = 0; s < NUM_SLOTS; s++) {
        u32 o = wrap(base_off + s * (u32)SLOT_SIZE + (u32)SLOT_SIZE - 4U);
        if (*(volatile u32 *)(ring + o) == SENTINEL)
            *(volatile u32 *)(ring + o) = 0;
    }

    if (target == 0xffffffffU) { iounmap(ring); return -11; }

    u32 s_off = wrap(base_off + target * (u32)SLOT_SIZE);
    u32 pts[5] = { 0x0010, 0x3C00, 0x7800, 0xB400, 0xEFFC };
    u32 orig[5], seen[5] = {0,0,0,0,0};
    for (int i = 0; i < 5; i++) {
        u32 o = wrap(s_off + pts[i]);
        orig[i] = *(volatile u32 *)(ring + o);
        *(volatile u32 *)(ring + o) = SENTINEL;
    }
    __asm__ volatile("dmb ishst" ::: "memory");

    _printk("\0016[earc_subms_diag] watching slot=%u (off=0x%x)\n", target, s_off);
    int all_done = 0;
    for (u32 t = 0; t < 300; t++) {
        usleep_range_state(200, 300, 2);
        __asm__ volatile("dmb ishld" ::: "memory");
        for (int i = 0; i < 5; i++) {
            if (!seen[i]) {
                u32 o = wrap(s_off + pts[i]);
                u32 v = *(volatile u32 *)(ring + o);
                if (v != SENTINEL) {
                    seen[i] = 1;
                    all_done++;
                    _printk("\0016[earc_subms_diag] tick=%u (~%uus) pt[%d]=+0x%x written! val=%08x\n",
                            t, t * 250, i, pts[i], v);
                }
            }
        }
        if (all_done == 5) break;
    }

    /* Now plant sentinel at +0x0010 and +0x7800 AFTER +0xEFFC was written, wait 18ms, check if overwritten */
    u32 o0 = wrap(s_off + 0x0010);
    u32 o2 = wrap(s_off + 0x7800);
    u32 v0 = *(volatile u32 *)(ring + o0);
    u32 v2 = *(volatile u32 *)(ring + o2);
    *(volatile u32 *)(ring + o0) = SENTINEL;
    *(volatile u32 *)(ring + o2) = SENTINEL;
    __asm__ volatile("dmb ishst" ::: "memory");
    usleep_range_state(18000, 19000, 2);
    __asm__ volatile("dmb ishld" ::: "memory");
    u32 a0 = *(volatile u32 *)(ring + o0);
    u32 a2 = *(volatile u32 *)(ring + o2);
    if (a0 == SENTINEL) *(volatile u32 *)(ring + o0) = v0;
    if (a2 == SENTINEL) *(volatile u32 *)(ring + o2) = v2;
    __asm__ volatile("dmb ishst" ::: "memory");

    _printk("\0016[earc_subms_diag] post-18ms check: +0x10=%s +0x7800=%s\n",
            (a0 == SENTINEL) ? "CLEAN_UNTOUCHED" : "OVERWRITTEN!",
            (a2 == SENTINEL) ? "CLEAN_UNTOUCHED" : "OVERWRITTEN!");

    iounmap(ring);
    return -11;
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

__attribute__((section(".gnu.linkonce.this_module"), used, aligned(64)))
struct v670_module __this_module = {
    .name = "earc_subms_diag",
    .init = init_module,
    .exit = cleanup_module,
};

struct modversion_info {
    u64 crc;
    char name[56];
};

__attribute__((section("__versions"), used, aligned(8)))
static const struct modversion_info ____versions[] = {
    { 0xb82e8f7bULL, "module_layout" },
    { 0x92997ed8ULL, "_printk" },
    { 0x6b4b2933ULL, "__ioremap" },
    { 0xedc03953ULL, "iounmap" },
    { 0xc3055d20ULL, "usleep_range_state" },
    { 0xaf56600aULL, "arm64_use_ng_mappings" },
};

__attribute__((section(".modinfo"), used)) static const char __mi1[] = "license=GPL";
__attribute__((section(".modinfo"), used)) static const char __mi2[] = "name=earc_subms_diag";
__attribute__((section(".modinfo"), used)) static const char __mi3[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
