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
#define RING_SIZE 0x00010000ULL

int init_module(void)
{
    u64 prot = 0x006800000000070bULL;
    if (arm64_use_ng_mappings)
        prot |= 0x800ULL;

    volatile u32 *ring = (volatile u32 *)__ioremap(RING_PHYS, RING_SIZE, prot);
    if (!ring) {
        _printk("\0016[earc_ring_probe] ioremap failed\n");
        return -12;
    }

    u32 orig0 = ring[0];
    u32 orig_sync = ring[0x7800 / 4]; /* 0x224A3800 */
    u32 orig_pay  = ring[0x7810 / 4]; /* 0x224A3810 */

    ring[0] = 0xDEADBEEFU;
    ring[0x7810 / 4] = 0xCAFEBABEU;
    __asm__ volatile("dmb ishst" ::: "memory");

    msleep(700);
    __asm__ volatile("dmb ishld" ::: "memory");

    u32 after0 = ring[0];
    u32 after_sync = ring[0x7800 / 4];
    u32 after_pay  = ring[0x7810 / 4];

    /* Restore if not overwritten */
    if (after0 == 0xDEADBEEFU) ring[0] = orig0;
    if (after_pay == 0xCAFEBABEU) ring[0x7810 / 4] = orig_pay;
    __asm__ volatile("dmb ishst" ::: "memory");

    _printk("\0016[earc_ring_probe] 0x2249C000: orig=%08x -> after700ms=%08x (%s)\n",
            orig0, after0, (after0 == 0xDEADBEEFU) ? "STATIC/NOT_WRITTEN" : "OVERWRITTEN_BY_DSP!");
    _printk("\0016[earc_ring_probe] 0x224A3810: orig=%08x -> after700ms=%08x (%s) sync=%08x->%08x\n",
            orig_pay, after_pay, (after_pay == 0xCAFEBABEU) ? "STATIC/NOT_WRITTEN" : "OVERWRITTEN_BY_DSP!",
            orig_sync, after_sync);

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

__attribute__((section(".gnu.linkonce.this_module"), used, aligned(64)))
struct v670_module __this_module = {
    .name = "earc_ring_probe",
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
__attribute__((section(".modinfo"), used)) static const char __mi2[] = "name=earc_ring_probe";
__attribute__((section(".modinfo"), used)) static const char __mi3[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
