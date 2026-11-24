/*
 * earc_mat_bypass3.ko - Low-jitter (0.25ms hrtimer) V670 C9K internal Android -> eARC TX
 * lossless TrueHD Dolby Atmos MAT bypass.
 */

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long ssize_t;
typedef long loff_t;
typedef unsigned long size_t;

struct file;
struct inode;
struct kiocb;
struct iov_iter;

extern int _printk(const char *fmt, ...);
extern void *__ioremap(u64 phys_addr, u64 size, u64 prot);
extern void iounmap(volatile void *addr);
extern void *vmalloc(u64 size);
extern void vfree(const void *addr);
extern void usleep_range_state(unsigned long min, unsigned long max, unsigned int state);
extern size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i);
extern int misc_register(void *misc);
extern void misc_deregister(void *misc);
extern u8 arm64_use_ng_mappings;

__attribute__((section(".plt"), used)) void __plt_stub(void) {}
__attribute__((section(".init.plt"), used)) void __init_plt_stub(void) {}
__attribute__((section(".text.ftrace_trampoline"), used)) void __ftrace_trampoline_stub(void) {}

__attribute__((aligned(4096), used))
void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

#define RING_PHYS 0x2249C000ULL
#define RING_SIZE 0x001A4000ULL /* 28 * 0xF000 */
#define SLOT_SIZE 0x0000F000ULL /* 61440 bytes */
#define NUM_SLOTS 28U
#define SENTINEL  0xCAFEBABEU
#define TASK_UNINTERRUPTIBLE 2U

static volatile u8 *g_ring;
static u8 *g_stage;
static u32 g_base_off;
static u32 g_next_slot;
static u32 g_frames_written;
static u32 g_already_ready;
static u32 g_timeouts;
static int g_locked;

static inline u32 slot_start_off(u32 s)
{
    return (g_base_off + s * (u32)SLOT_SIZE) % (u32)RING_SIZE;
}

static inline u32 slot_end_off(u32 s)
{
    return (g_base_off + s * (u32)SLOT_SIZE + (u32)SLOT_SIZE - 4U) % (u32)RING_SIZE;
}

static inline u32 ring_read32(u32 off)
{
    return *(volatile u32 *)(g_ring + off);
}

static inline void ring_write32(u32 off, u32 val)
{
    *(volatile u32 *)(g_ring + off) = val;
}

static void ring_write_slot(u32 s, const u8 *src)
{
    u32 off = slot_start_off(s);
    u32 first = (u32)RING_SIZE - off;
    if (first > (u32)SLOT_SIZE)
        first = (u32)SLOT_SIZE;

    const u64 *s64 = (const u64 *)src;
    volatile u64 *d64 = (volatile u64 *)(g_ring + off);
    u32 n1 = first / 8U;
    for (u32 i = 0; i < n1; i++)
        d64[i] = s64[i];

    u32 rem = (u32)SLOT_SIZE - first;
    if (rem > 0) {
        volatile u64 *d64_wrap = (volatile u64 *)g_ring;
        const u64 *s64_wrap = s64 + n1;
        u32 n2 = rem / 8U;
        for (u32 i = 0; i < n2; i++)
            d64_wrap[i] = s64_wrap[i];
    }
    __asm__ volatile("dmb ishst" ::: "memory");
}

static int lock_onto_ring(void)
{
    g_base_off = 0xffffffffU;
    for (u32 off = 0; off < (u32)SLOT_SIZE; off += 4U) {
        if (ring_read32(off) == 0x4e1ff872U) {
            g_base_off = off;
            break;
        }
    }
    if (g_base_off == 0xffffffffU)
        return -19;

    for (u32 s = 0; s < NUM_SLOTS; s++)
        ring_write32(slot_end_off(s), SENTINEL);
    __asm__ volatile("dmb ishst" ::: "memory");

    for (u32 t = 0; t < 300; t++) {
        usleep_range_state(200, 300, TASK_UNINTERRUPTIBLE);
        __asm__ volatile("dmb ishld" ::: "memory");
        for (u32 s = 0; s < NUM_SLOTS; s++) {
            if (ring_read32(slot_end_off(s)) != SENTINEL) {
                g_next_slot = (s + 1U) % NUM_SLOTS;
                g_locked = 1;
                for (u32 k = 0; k < NUM_SLOTS; k++) {
                    if (k != g_next_slot && ring_read32(slot_end_off(k)) == SENTINEL)
                        ring_write32(slot_end_off(k), 0);
                }
                __asm__ volatile("dmb ishst" ::: "memory");
                _printk("\0016[earc_mat_bypass3] locked! base_off=0x%x det=%u next=%u\n",
                        g_base_off, s, g_next_slot);
                return 0;
            }
        }
    }
    return -110;
}

static int dev_open(struct inode *inode, struct file *filp)
{
    (void)inode;
    (void)filp;
    g_frames_written = 0;
    g_already_ready = 0;
    g_timeouts = 0;
    g_locked = 0;
    return lock_onto_ring();
}

static int dev_release(struct inode *inode, struct file *filp)
{
    (void)inode;
    (void)filp;
    if (g_locked && ring_read32(slot_end_off(g_next_slot)) == SENTINEL)
        ring_write32(slot_end_off(g_next_slot), 0);
    g_locked = 0;
    _printk("\0016[earc_mat_bypass3] closed: frames=%u already_ready=%u timeouts=%u\n",
            g_frames_written, g_already_ready, g_timeouts);
    return 0;
}

static ssize_t dev_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
    (void)iocb;
    if (!g_locked)
        return -19;

    size_t copied = _copy_from_iter(g_stage, SLOT_SIZE, from);
    if (copied < SLOT_SIZE)
        return copied ? (ssize_t)copied : -14;

    u32 end_off = slot_end_off(g_next_slot);
    __asm__ volatile("dmb ishld" ::: "memory");
    int ready = 0;
    if (ring_read32(end_off) != SENTINEL) {
        g_already_ready++;
        ready = 1;
    } else {
        /* Sleep 14ms first if we just wrote the previous frame? No, poll at 250us so we never overshoot! */
        for (u32 t = 0; t < 200; t++) {
            usleep_range_state(200, 300, TASK_UNINTERRUPTIBLE);
            __asm__ volatile("dmb ishld" ::: "memory");
            if (ring_read32(end_off) != SENTINEL) {
                ready = 1;
                break;
            }
        }
    }

    if (!ready) {
        g_timeouts++;
        if (lock_onto_ring() != 0)
            return -110;
    }

    /* Immediately overwrite g_next_slot within <0.3ms of DEC-R2 finishing it */
    ring_write_slot(g_next_slot, g_stage);

    /* Arm next slot's sentinel immediately */
    g_next_slot = (g_next_slot + 1U) % NUM_SLOTS;
    ring_write32(slot_end_off(g_next_slot), SENTINEL);
    __asm__ volatile("dmb ishst" ::: "memory");

    g_frames_written++;
    return (ssize_t)SLOT_SIZE;
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

extern struct v670_module __this_module;

struct v670_file_operations {
    struct v670_module *owner;
    u64 llseek;
    u64 read;
    u64 write;
    u64 read_iter;
    ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
    u8 pad1[0x70 - 0x30];
    int (*open)(struct inode *, struct file *);
    u64 flush;
    int (*release)(struct inode *, struct file *);
    u8 pad2[0x120 - 0x88];
};

static const struct v670_file_operations earc_fops = {
    .owner = &__this_module,
    .write_iter = dev_write_iter,
    .open = dev_open,
    .release = dev_release,
};

struct v670_miscdevice {
    int minor;
    u32 pad0;
    const char *name;
    const struct v670_file_operations *fops;
    u8 rest[0x50 - 0x18];
};

static struct v670_miscdevice earc_miscdev = {
    .minor = 255,
    .name = "earc_mat_bypass3",
    .fops = &earc_fops,
};

int init_module(void)
{
    u64 prot = 0x006800000000070bULL;
    if (arm64_use_ng_mappings)
        prot |= 0x800ULL;

    g_ring = (volatile u8 *)__ioremap(RING_PHYS, RING_SIZE, prot);
    if (!g_ring)
        return -12;

    g_stage = (u8 *)vmalloc(SLOT_SIZE);
    if (!g_stage) {
        iounmap(g_ring);
        g_ring = 0;
        return -12;
    }

    int rc = misc_register(&earc_miscdev);
    if (rc) {
        vfree(g_stage);
        iounmap(g_ring);
        g_stage = 0;
        g_ring = 0;
        return rc;
    }

    _printk("\0016[earc_mat_bypass3] READY: /dev/earc_mat_bypass3\n");
    return 0;
}

void cleanup_module(void)
{
    misc_deregister(&earc_miscdev);
    if (g_stage) {
        vfree(g_stage);
        g_stage = 0;
    }
    if (g_ring) {
        iounmap(g_ring);
        g_ring = 0;
    }
    _printk("\0016[earc_mat_bypass3] unloaded\n");
}

__attribute__((section(".gnu.linkonce.this_module"), used, aligned(64)))
struct v670_module __this_module = {
    .name = "earc_mat_bypass3",
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
    { 0xd6ee688fULL, "vmalloc" },
    { 0x999e8297ULL, "vfree" },
    { 0xc3055d20ULL, "usleep_range_state" },
    { 0xaf56600aULL, "arm64_use_ng_mappings" },
    { 0x1e85aa79ULL, "misc_register" },
    { 0xe1d7aeb8ULL, "misc_deregister" },
    { 0x4efa65a3ULL, "_copy_from_iter" },
};

__attribute__((section(".modinfo"), used)) static const char __mi1[] = "license=GPL";
__attribute__((section(".modinfo"), used)) static const char __mi2[] = "name=earc_mat_bypass3";
__attribute__((section(".modinfo"), used)) static const char __mi3[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
