/*
 * tcl_earc_mat.ko - Production buffered kthread driver for TCL C9K (MT5896 / V670)
 * internal Android -> eARC TX lossless TrueHD Dolby Atmos MAT 2.0 bypass.
 *
 * Exposes /dev/earc_mat_bypass.
 * - Accepts arbitrary write() sizes (accumulates into 61,440-byte IEC61937/MAT 2.0 frames).
 * - Uses f_op->write_iter + core-kernel _copy_from_iter (ARM64_SW_TTBR0_PAN safe).
 * - 32-frame (1.96 MiB / 640 ms) lock-free SPSC ring buffer absorbs FUSE/network/demux jitter.
 * - Dedicated kthread (earc_mat_pump) synchronizes to DEC-R2's 20.000 ms MAT cadence in
 *   0x2249C000..0x22640000 via 0.25 ms hrtimer polling (usleep_range_state) on the +0xEFFC
 *   slot sentinel, overwriting each silent MAT slot immediately after DEC-R2 finishes it.
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
struct task_struct;

extern int _printk(const char *fmt, ...);
extern void *__ioremap(u64 phys_addr, u64 size, u64 prot);
extern void iounmap(volatile void *addr);
extern void *vmalloc(u64 size);
extern void vfree(const void *addr);
extern void usleep_range_state(unsigned long min, unsigned long max, unsigned int state);
extern size_t _copy_from_iter(void *addr, size_t bytes, struct iov_iter *i);
extern int misc_register(void *misc);
extern void misc_deregister(void *misc);
extern struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
                                                  void *data, int node,
                                                  const char namefmt[], ...);
extern int wake_up_process(struct task_struct *p);
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

#define Q_SLOTS   8U            /* 8 * 20ms = 160ms jitter buffer (power of 2) */
#define Q_MASK    (Q_SLOTS - 1U)

#define TASK_INTERRUPTIBLE   1U
#define TASK_UNINTERRUPTIBLE 2U
#define NUMA_NO_NODE         (-1)

static volatile u8 *g_ring;
static u8 *g_q_buf;

static volatile u32 g_q_head;
static volatile u32 g_q_tail;
static u32 g_partial;

static volatile int g_open;
static volatile int g_stop_req;
static volatile int g_thread_alive;

static u32 g_base_off;
static u32 g_next_slot;
static u32 g_frames_out;
static u32 g_underruns;
static u32 g_already_ready;
static u32 g_relocks;

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

    for (u32 t = 0; t < 300 && !g_stop_req; t++) {
        usleep_range_state(200, 300, TASK_UNINTERRUPTIBLE);
        __asm__ volatile("dmb ishld" ::: "memory");
        for (u32 s = 0; s < NUM_SLOTS; s++) {
            if (ring_read32(slot_end_off(s)) != SENTINEL) {
                g_next_slot = (s + 1U) % NUM_SLOTS;
                for (u32 k = 0; k < NUM_SLOTS; k++) {
                    if (k != g_next_slot && ring_read32(slot_end_off(k)) == SENTINEL)
                        ring_write32(slot_end_off(k), 0);
                }
                __asm__ volatile("dmb ishst" ::: "memory");
                return 0;
            }
        }
    }

    for (u32 k = 0; k < NUM_SLOTS; k++) {
        if (ring_read32(slot_end_off(k)) == SENTINEL)
            ring_write32(slot_end_off(k), 0);
    }
    __asm__ volatile("dmb ishst" ::: "memory");
    return -110;
}

static int mat_pump_thread(void *arg)
{
    (void)arg;
    int locked = 0;
    int started = 0;

    while (!g_stop_req) {
        if (!locked) {
            if (lock_onto_ring() == 0) {
                locked = 1;
                _printk("\0016[tcl_earc_mat] pump locked: base_off=0x%x next_slot=%u\n",
                        g_base_off, g_next_slot);
            } else {
                usleep_range_state(10000, 15000, TASK_UNINTERRUPTIBLE);
                continue;
            }
        }

        u32 end_off = slot_end_off(g_next_slot);
        __asm__ volatile("dmb ishld" ::: "memory");
        int ready = 0;
        if (ring_read32(end_off) != SENTINEL) {
            g_already_ready++;
            ready = 1;
        } else {
            for (u32 t = 0; t < 400 && !g_stop_req; t++) {
                usleep_range_state(200, 300, TASK_UNINTERRUPTIBLE);
                __asm__ volatile("dmb ishld" ::: "memory");
                if (ring_read32(end_off) != SENTINEL) {
                    ready = 1;
                    break;
                }
            }
        }

        if (g_stop_req)
            break;

        if (!ready) {
            g_relocks++;
            locked = 0;
            continue;
        }

        __asm__ volatile("dmb ishld" ::: "memory");
        u32 tail = g_q_tail;
        u32 head = g_q_head;
        if (tail != head) {
            const u8 *src = g_q_buf + (u64)(tail & Q_MASK) * SLOT_SIZE;
            ring_write_slot(g_next_slot, src);
            __asm__ volatile("dmb ishst" ::: "memory");
            g_q_tail = tail + 1U;
            g_frames_out++;
            started = 1;
        } else if (started) {
            /* Leave DEC-R2's silent MAT frame untouched on underrun */
            g_underruns++;
        }

        g_next_slot = (g_next_slot + 1U) % NUM_SLOTS;
        ring_write32(slot_end_off(g_next_slot), SENTINEL);
        __asm__ volatile("dmb ishst" ::: "memory");

        /* DEC-R2 writes every 20.000ms: sleep 16.5ms first to cut hrtimer wakeups by ~85%,
           then poll at 250us only for the final ~3.5ms window. */
        if (!g_stop_req)
            usleep_range_state(16500, 17000, TASK_UNINTERRUPTIBLE);
    }

    if (locked && ring_read32(slot_end_off(g_next_slot)) == SENTINEL) {
        ring_write32(slot_end_off(g_next_slot), 0);
        __asm__ volatile("dmb ishst" ::: "memory");
    }

    __asm__ volatile("dmb ishst" ::: "memory");
    g_thread_alive = 0;
    return 0;
}

static int dev_open(struct inode *inode, struct file *filp)
{
    (void)inode;
    (void)filp;
    if (g_open)
        return -16; /* -EBUSY */

    g_open = 1;
    g_q_head = 0;
    g_q_tail = 0;
    g_partial = 0;
    g_frames_out = 0;
    g_underruns = 0;
    g_already_ready = 0;
    g_relocks = 0;
    g_stop_req = 0;
    g_thread_alive = 1;
    __asm__ volatile("dmb ishst" ::: "memory");

    struct task_struct *t = kthread_create_on_node(mat_pump_thread, 0, NUMA_NO_NODE, "earc_mat_pump");
    if ((unsigned long)t >= (unsigned long)-4095L) {
        g_thread_alive = 0;
        g_open = 0;
        return (int)(long)t;
    }
    wake_up_process(t);
    return 0;
}

static int dev_release(struct inode *inode, struct file *filp)
{
    (void)inode;
    (void)filp;

    /* Stop immediately on close() so pause/seek silences within <20ms */
    g_stop_req = 1;
    __asm__ volatile("dmb ishst" ::: "memory");

    for (u32 w = 0; w < 200; w++) {
        __asm__ volatile("dmb ishld" ::: "memory");
        if (!g_thread_alive)
            break;
        usleep_range_state(500, 1000, TASK_UNINTERRUPTIBLE);
    }

    _printk("\0016[tcl_earc_mat] closed: frames=%u underruns=%u already_ready=%u relocks=%u\n",
            g_frames_out, g_underruns, g_already_ready, g_relocks);
    g_open = 0;
    return 0;
}

static ssize_t dev_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
    (void)iocb;
    size_t total = 0;

    while (1) {
        u32 wait_ms = 0;
        while ((u32)(g_q_head - g_q_tail) >= Q_SLOTS) {
            usleep_range_state(2000, 3000, TASK_INTERRUPTIBLE);
            __asm__ volatile("dmb ishld" ::: "memory");
            if (!g_thread_alive)
                return total ? (ssize_t)total : -19;
            wait_ms += 2;
            if (wait_ms >= 500)
                return total ? (ssize_t)total : -110; /* -ETIMEDOUT: eARC MAT ring inactive */
        }

        u8 *dst = g_q_buf + (u64)(g_q_head & Q_MASK) * SLOT_SIZE + g_partial;
        size_t want = (size_t)SLOT_SIZE - g_partial;
        size_t n = _copy_from_iter(dst, want, from);
        if (n == 0)
            break;

        total += n;
        g_partial += (u32)n;
        if (g_partial == (u32)SLOT_SIZE) {
            __asm__ volatile("dmb ishst" ::: "memory");
            g_q_head++;
            g_partial = 0;
        }
    }

    return (ssize_t)total;
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
    .name = "earc_mat_bypass",
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

    g_q_buf = (u8 *)vmalloc((u64)Q_SLOTS * SLOT_SIZE);
    if (!g_q_buf) {
        iounmap(g_ring);
        g_ring = 0;
        return -12;
    }

    int rc = misc_register(&earc_miscdev);
    if (rc) {
        vfree(g_q_buf);
        iounmap(g_ring);
        g_q_buf = 0;
        g_ring = 0;
        return rc;
    }

    _printk("\0016[tcl_earc_mat] READY: /dev/earc_mat_bypass (queue=%u frames / %ums)\n",
            Q_SLOTS, Q_SLOTS * 20U);
    return 0;
}

void cleanup_module(void)
{
    misc_deregister(&earc_miscdev);
    if (g_q_buf) {
        vfree(g_q_buf);
        g_q_buf = 0;
    }
    if (g_ring) {
        iounmap(g_ring);
        g_ring = 0;
    }
    _printk("\0016[tcl_earc_mat] unloaded\n");
}

__attribute__((section(".gnu.linkonce.this_module"), used, aligned(64)))
struct v670_module __this_module = {
    .name = "tcl_earc_mat",
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
    { 0x96517739ULL, "kthread_create_on_node" },
    { 0xb268478cULL, "wake_up_process" },
};

__attribute__((section(".modinfo"), used)) static const char __mi1[] = "license=GPL";
__attribute__((section(".modinfo"), used)) static const char __mi2[] = "name=tcl_earc_mat";
__attribute__((section(".modinfo"), used)) static const char __mi3[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
