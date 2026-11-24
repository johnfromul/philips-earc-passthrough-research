/*
 * Read-only 200ms live diff of ALL Vivaldi RIU banks (0x950000..0x950dff, 0x951000..0x9515ff)
 * and DEC_R2_SHARE_MEM (phys 0x21b11000, 8 KiB) to find every hardware register / SHM word
 * advancing alongside the 0x2249C000 MAT ring during HDMI Dolby Atmos playback.
 */
typedef unsigned char u8;
typedef unsigned short u16;
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
__attribute__((aligned(4096), used)) void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

/* Vivaldi banks in /mtk-tv-offload-platform:
 * 0x950000..0x950a00 (11 banks = 0x1600 phys bytes from 0x1d2a0000)
 * 0x950d00 (0x1d2a1a00, 0x200 bytes)
 * 0x951000, 0x951100 (0x1d2a2000, 0x400 bytes)
 * 0x951500 (0x1d2a2a00, 0x200 bytes)
 */
static const u32 bank_riu[] = {
    0x950000, 0x950100, 0x950200, 0x950300, 0x950400,
    0x950500, 0x950600, 0x950700, 0x950800, 0x950900,
    0x950a00, 0x950d00, 0x951000, 0x951100, 0x951500,
};
#define NUM_BANKS 15

static u16 snap_riu[NUM_BANKS][128];
static u32 snap_shm[2048];

int init_module(void)
{
    u64 prot_nc = 0x006800000000070bULL | (arm64_use_ng_mappings ? 0x800ULL : 0ULL);
    u64 prot_dev = 0x0068000000000703ULL | (arm64_use_ng_mappings ? 0x800ULL : 0ULL);

    volatile u32 *va[NUM_BANKS];
    for (int b = 0; b < NUM_BANKS; b++) {
        u64 phys = 0x1c000000ULL + ((u64)bank_riu[b] << 1);
        va[b] = (volatile u32 *)__ioremap(phys, 0x200ULL, prot_dev);
        if (va[b]) {
            for (int i = 0; i < 128; i++)
                snap_riu[b][i] = (u16)(va[b][i] & 0xffffU);
        }
    }

    volatile u32 *shm = (volatile u32 *)__ioremap(0x21b11000ULL, 0x2000ULL, prot_nc);
    if (shm) {
        for (int i = 0; i < 2048; i++) snap_shm[i] = shm[i];
    }

    msleep(200);

    _printk("\0016[doorbell_diff] === ALL Vivaldi RIU changes over 200ms ===\n");
    int riu_logged = 0;
    for (int b = 0; b < NUM_BANKS; b++) {
        if (!va[b]) continue;
        for (int i = 0; i < 128; i++) {
            u16 cur = (u16)(va[b][i] & 0xffffU);
            if (cur != snap_riu[b][i]) {
                if (riu_logged < 60) {
                    _printk("\0016[doorbell_diff] RIU 0x%06x: 0x%04x -> 0x%04x (delta=%d)\n",
                            bank_riu[b] + i * 2, snap_riu[b][i], cur, (int)cur - (int)snap_riu[b][i]);
                }
                riu_logged++;
            }
        }
        iounmap(va[b]);
    }

    _printk("\0016[doorbell_diff] === DEC_R2_SHM changes over 200ms ===\n");
    int shm_logged = 0;
    if (shm) {
        for (int i = 0; i < 2048; i++) {
            u32 cur = shm[i];
            if (cur != snap_shm[i]) {
                if (shm_logged < 35) {
                    _printk("\0016[doorbell_diff] SHM +0x%04x: 0x%08x -> 0x%08x (delta=0x%x)\n",
                            i * 4, snap_shm[i], cur, cur - snap_shm[i]);
                }
                shm_logged++;
            }
        }
        iounmap(shm);
    }

    _printk("\0016[doorbell_diff] DONE: riu_changed=%d shm_changed=%d\n", riu_logged, shm_logged);
    return -11;
}

void cleanup_module(void) {}

struct v670_module { u8 p0[0x18]; char name[56]; u8 p1[0x178-0x50]; int (*init)(void); u8 p2[0x378-0x180]; void (*exit)(void); u8 p3[0x40]; };
__attribute__((section(".gnu.linkonce.this_module"), used)) struct v670_module __this_module = { .name = "earc_doorbell_diff", .init = init_module, .exit = cleanup_module };
struct modversion_info { u64 crc; char name[56]; };
__attribute__((section("__versions"), used, aligned(8))) static const struct modversion_info ____versions[] = {
    { 0xb82e8f7bULL, "module_layout" }, { 0x92997ed8ULL, "_printk" },
    { 0x6b4b2933ULL, "__ioremap" }, { 0xedc03953ULL, "iounmap" }, { 0xaf56600aULL, "arm64_use_ng_mappings" },
    { 0xf9a482f9ULL, "msleep" },
};
__attribute__((section(".modinfo"), used, aligned(1))) static const char m1[] = "license=GPL v2";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m2[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m3[] = "name=earc_doorbell_diff";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m4[] = "depends=";
