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
#define RING_SIZE 0x001A4000ULL /* 28 * 0xF000 */
#define SLOT_SIZE 0x0000F000ULL /* 61440 */
#define NUM_SLOTS 28U
#define SENTINEL  0xCAFEBABEU

int init_module(void)
{
    u64 prot = 0x006800000000070bULL;
    if (arm64_use_ng_mappings)
        prot |= 0x800ULL;

    volatile u32 *ring = (volatile u32 *)__ioremap(RING_PHYS, RING_SIZE, prot);
    if (!ring) {
        _printk("\0016[earc_slot_timing] ioremap failed\n");
        return -12;
    }

    /* Find first sync word offset in [0 .. SLOT_SIZE) */
    u32 base_off = 0xffffffffU;
    for (u32 i = 0; i < SLOT_SIZE / 4; i++) {
        if (ring[i] == 0x4e1ff872U) {
            base_off = i * 4;
            break;
        }
    }
    _printk("\0016[earc_slot_timing] base_off=0x%x\n", base_off);
    if (base_off == 0xffffffffU) {
        iounmap(ring);
        return -11;
    }

    /* Save original end-word of each slot and plant sentinel */
    u32 orig[NUM_SLOTS];
    u32 done[NUM_SLOTS];
    for (u32 s = 0; s < NUM_SLOTS; s++) {
        u32 off = (base_off + s * SLOT_SIZE + SLOT_SIZE - 4) % RING_SIZE;
        orig[s] = ring[off / 4];
        done[s] = 0;
        ring[off / 4] = SENTINEL;
    }
    __asm__ volatile("dmb ishst" ::: "memory");

    /* Poll for 300ms (every 2ms) to see exact order & cadence of DEC-R2 writes */
    for (u32 t = 0; t < 150; t++) {
        msleep(2);
        __asm__ volatile("dmb ishld" ::: "memory");
        for (u32 s = 0; s < NUM_SLOTS; s++) {
            if (!done[s]) {
                u32 off = (base_off + s * SLOT_SIZE + SLOT_SIZE - 4) % RING_SIZE;
                u32 v = ring[off / 4];
                if (v != SENTINEL) {
                    done[s] = 1;
                    _printk("\0016[earc_slot_timing] t=%ums slot=%u (off=0x%x) written! val=%08x\n",
                            t * 2, s, (base_off + s * SLOT_SIZE) % (u32)RING_SIZE, v);
                }
            }
        }
    }

    /* Restore any untouched sentinels */
    for (u32 s = 0; s < NUM_SLOTS; s++) {
        if (!done[s]) {
            u32 off = (base_off + s * SLOT_SIZE + SLOT_SIZE - 4) % RING_SIZE;
            if (ring[off / 4] == SENTINEL)
                ring[off / 4] = orig[s];
        }
    }
    __asm__ volatile("dmb ishst" ::: "memory");

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
    .name = "earc_slot_timing",
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
    { 0xf9a482f9ULL, "msleep" },
    { 0xaf56600aULL, "arm64_use_ng_mappings" },
};

__attribute__((section(".modinfo"), used)) static const char __mi1[] = "license=GPL";
__attribute__((section(".modinfo"), used)) static const char __mi2[] = "name=earc_slot_timing";
__attribute__((section(".modinfo"), used)) static const char __mi3[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
