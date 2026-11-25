/* MAT output for the verified TPM231WW .214 eARC DSP route. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long s64;
typedef unsigned long size_t;
typedef long ssize_t;
typedef long long loff_t;

struct inode;
struct file;
struct kiocb;
struct iov_iter;
struct task_struct;

extern int _printk(const char *, ...);
extern void *__ioremap(u64, u64, u64);
extern void iounmap(volatile void *);
extern u8 arm64_use_ng_mappings;
extern u8 mtk_offload_read_reg_byte(u32);
extern s64 ktime_get(void);
extern void usleep_range_state(u64, u64, u32);
extern size_t _copy_from_iter(void *, size_t, struct iov_iter *);
extern ssize_t simple_read_from_buffer(void *, size_t, loff_t *, const void *, size_t);
extern struct task_struct *kthread_create_on_node(int (*)(void *), void *, int, const char *, ...);
extern void kthread_bind(struct task_struct *, unsigned int);
extern int wake_up_process(struct task_struct *);
extern int kthread_should_stop(void);
extern int kthread_stop(struct task_struct *);
extern void sched_set_fifo_low(struct task_struct *);

#define LOG "\0016philips_earc_mat: "
#define SLOT_BYTES 0xf000U
#define RING_SLOTS 28U
#define RING_PHYS 0x94596800UL
#define QUEUE_SLOTS 8U
#define MARKER 0x5a17b04dU
#define IDLE_TASK 0x402U
#define FMODE_WRITE 2U
#define FMODE_STREAM 0x00200000U
#define POSITION_FLAGS 0x0000801cU
#define EINVAL 22
#define ENODEV 19
#define EBUSY 16
#define ENOMEM 12
#define ETIMEDOUT 110

static volatile u32 *ring;
static u64 queue[QUEUE_SLOTS][SLOT_BYTES / sizeof(u64)];
static struct task_struct *pump_task;
static u32 head, tail, partial, owner, writing, draining;
static u64 frames_out, last_slot_ns;
static u32 underruns, late, relocks;

struct earc_mat_status {
    u64 frames_out, last_slot_mono_ns;
    u32 q_depth, underruns, already_ready, relocks, ring_active, reserved;
};

static u32 acquire(u32 *value) { return __atomic_load_n(value, __ATOMIC_ACQUIRE); }
static void release(u32 *value, u32 next) { __atomic_store_n(value, next, __ATOMIC_RELEASE); }
static u32 queued(void)
{
    u32 consumed = acquire(&tail);
    return acquire(&head) - consumed;
}

static int selected(void)
{
    return (mtk_offload_read_reg_byte(0x951115) & 0x70U) == 0x20U &&
           (mtk_offload_read_reg_byte(0x95038f) & 0x02U) != 0;
}

static u32 read_word(volatile u32 *word)
{
    u32 value = *word;
    __asm__ volatile("dmb oshld" ::: "memory");
    return value;
}

static volatile u32 *slot_at(u32 slot) { return ring + slot * (SLOT_BYTES / 4); }
static volatile u32 *end_at(u32 slot) { return slot_at(slot) + SLOT_BYTES / 4 - 1; }

static int stock_header(u32 slot)
{
    return read_word(slot_at(slot)) == 0x4e1ff872U && read_word(slot_at(slot) + 1) == 0xeff00016U;
}

static int arm_slot(u32 slot)
{
    if (!selected() || !stock_header(slot) || read_word(end_at(slot)))
        return 0;
    *end_at(slot) = MARKER;
    __asm__ volatile("dsb st" ::: "memory");
    return 1;
}

static void cancel_marker(u32 slot)
{
    if (read_word(end_at(slot)) == MARKER) {
        *end_at(slot) = 0;
        __asm__ volatile("dsb st" ::: "memory");
    }
}

static void write_slot(u32 slot, const u64 *source)
{
    volatile u64 *destination = (volatile u64 *)slot_at(slot);
    for (u32 i = 0; i < SLOT_BYTES / 8; i += 4) {
        __asm__ volatile("stp %0, %1, [%4]\n\tstp %2, %3, [%4, #16]"
                         :: "r"(source[i]), "r"(source[i+1]), "r"(source[i+2]),
                            "r"(source[i+3]), "r"(destination + i) : "memory");
    }
    __asm__ volatile("dsb st" ::: "memory");
}

static s64 synchronize(void)
{
    if (!arm_slot(0))
        return 0;
    s64 limit = ktime_get() + 700000000L;
    while (!kthread_should_stop() && ktime_get() < limit && selected()) {
        if (read_word(end_at(0)) == 0)
            return ktime_get();
        usleep_range_state(100, 200, IDLE_TASK);
    }
    cancel_marker(0);
    return 0;
}

static s64 await_slot(u32 slot, s64 previous, int *in_time)
{
    s64 sleep_us = (previous + 14500000L - ktime_get()) / 1000;
    if (sleep_us > 300)
        usleep_range_state((u64)sleep_us - 250, (u64)sleep_us, IDLE_TASK);
    s64 checked = ktime_get();
    int seen_marker = 0;
    const s64 deadline = previous + 24000000L;
    while (!kthread_should_stop() && ktime_get() < deadline) {
        s64 now = ktime_get();
        u32 value = read_word(end_at(slot));
        if (value == 0) {
            *in_time = seen_marker && now - checked <= 700000L;
            return now;
        }
        if (value != MARKER)
            break;
        seen_marker = 1;
        checked = now;
        __asm__ volatile("yield" ::: "memory");
    }
    return 0;
}

static int pump(void *unused)
{
    (void)unused;
    struct task_struct *self;
    __asm__ volatile("mrs %0, sp_el0" : "=r"(self));
    sched_set_fifo_low(self);
    s64 last = 0;
    u32 slot = 1;
    while (!kthread_should_stop()) {
        if (!selected() || (!last && queued() < (acquire(&draining) ? 1U : 2U))) {
            last = 0;
            usleep_range_state(1000, 1600, IDLE_TASK);
            continue;
        }
        if (!last) {
            last = synchronize();
            slot = 1;
            if (!last) {
                usleep_range_state(1000, 1600, IDLE_TASK);
                continue;
            }
        }
        if (!arm_slot(slot)) {
            __atomic_add_fetch(&relocks, 1, __ATOMIC_RELAXED);
            last = 0;
            continue;
        }
        int in_time = 0;
        last = await_slot(slot, last, &in_time);
        if (!last || !selected() || kthread_should_stop()) {
            cancel_marker(slot);
            __atomic_add_fetch(&relocks, 1, __ATOMIC_RELAXED);
            last = 0;
            continue;
        }
        u32 cursor = acquire(&tail);
        if (!in_time) {
            __atomic_add_fetch(&late, 1, __ATOMIC_RELAXED);
        } else if (cursor != acquire(&head) && stock_header(slot)) {
            write_slot(slot, queue[cursor % QUEUE_SLOTS]);
            __atomic_store_n(&last_slot_ns, (u64)last, __ATOMIC_RELEASE);
            __atomic_add_fetch(&frames_out, 1, __ATOMIC_RELEASE);
            release(&tail, cursor + 1U);
        } else {
            __atomic_add_fetch(&underruns, 1, __ATOMIC_RELAXED);
        }
        slot = (slot + 1U) % RING_SLOTS;
    }
    return 0;
}

static int writer_fd(struct file *file) { return *(void **)((u8 *)file + 0xd8) != (void *)0; }

static int dev_open(struct inode *unused, struct file *file)
{
    (void)unused;
    u32 *mode = (u32 *)((u8 *)file + 0x44);
    *mode = (*mode & ~POSITION_FLAGS) | FMODE_STREAM;
    *(void **)((u8 *)file + 0xd8) = (void *)0;
    if (!selected() || !stock_header(0) || !stock_header(27))
        return -ENODEV;
    if (!(*mode & FMODE_WRITE))
        return 0;
    if (__atomic_exchange_n(&owner, 1, __ATOMIC_ACQ_REL))
        return -EBUSY;

    head = tail = partial = draining = writing = 0;
    frames_out = last_slot_ns = 0;
    underruns = late = relocks = 0;
    struct task_struct *task = kthread_create_on_node(pump, (void *)0, -1, "philips_mat_pump");
    if ((u64)task >= (u64)-4095L) {
        release(&owner, 0);
        return (int)(long)task;
    }
    pump_task = task;
    kthread_bind(task, 2);
    *(void **)((u8 *)file + 0xd8) = (void *)1;
    wake_up_process(task);
    return 0;
}

static int dev_release(struct inode *unused, struct file *file)
{
    (void)unused;
    if (writer_fd(file)) {
        kthread_stop(pump_task);
        pump_task = (void *)0;
        *(void **)((u8 *)file + 0xd8) = (void *)0;
        _printk(LOG "closed frames=%lu late=%u underruns=%u relocks=%u\n", frames_out, late, underruns, relocks);
        release(&owner, 0);
    }
    return 0;
}

static ssize_t dev_read(struct file *unused, char *out, size_t bytes, loff_t *position)
{
    (void)unused;
    (void)position;
    loff_t start = 0;
    struct earc_mat_status status = {
        .frames_out = __atomic_load_n(&frames_out, __ATOMIC_ACQUIRE),
        .last_slot_mono_ns = __atomic_load_n(&last_slot_ns, __ATOMIC_ACQUIRE),
        .q_depth = queued(), .underruns = __atomic_load_n(&underruns, __ATOMIC_RELAXED),
        .already_ready = __atomic_load_n(&late, __ATOMIC_RELAXED),
        .relocks = __atomic_load_n(&relocks, __ATOMIC_RELAXED), .ring_active = (u32)selected(),
    };
    if (status.q_depth > QUEUE_SLOTS)
        status.q_depth = QUEUE_SLOTS;
    return simple_read_from_buffer(out, bytes, &start, &status, sizeof(status));
}

static ssize_t dev_write(struct kiocb *iocb, struct iov_iter *from)
{
    if (!writer_fd(*(struct file **)iocb) || !selected())
        return -ENODEV;
    if (__atomic_exchange_n(&writing, 1, __ATOMIC_ACQUIRE))
        return -EBUSY;
    ssize_t total = 0;
    ssize_t error = 0;
    const s64 deadline = ktime_get() + 1600000000L;

    while (*(size_t *)((u8 *)from + 0x10)) {
        if (queued() >= QUEUE_SLOTS) {
            if (!selected() || ktime_get() >= deadline) {
                error = -ETIMEDOUT;
                break;
            }
            usleep_range_state(700, 1100, IDLE_TASK);
            continue;
        }
        u32 cursor = acquire(&head);
        u8 *slot = (u8 *)queue[cursor % QUEUE_SLOTS];
        size_t bytes = _copy_from_iter(slot + partial, SLOT_BYTES - partial, from);
        if (!bytes)
            break;
        partial += (u32)bytes;
        total += (ssize_t)bytes;
        if (partial == SLOT_BYTES) {
            const u32 *words = (const u32 *)slot;
            partial = 0;
            if (words[0] != 0x4e1ff872U || words[1] != 0xeff00016U) {
                error = -EINVAL;
                break;
            }
            release(&head, cursor + 1U);
        }
    }
    release(&writing, 0);
    return error == -EINVAL || !total ? error : total;
}

static int dev_fsync(struct file *file, loff_t from, loff_t to, int datasync)
{
    (void)from;
    (void)to;
    (void)datasync;
    if (!writer_fd(file))
        return 0;
    release(&draining, 1);
    s64 limit = ktime_get() + 1600000000L;
    while (queued() && selected() && ktime_get() < limit)
        usleep_range_state(1000, 1500, IDLE_TASK);
    release(&draining, 0);
    return queued() ? -ETIMEDOUT : 0;
}

#include "kernel-abi.h"

__attribute__((section(".init.text"))) int init_module(void)
{
    u64 mapping = 0x006800000000070bUL | (arm64_use_ng_mappings ? 0x800UL : 0);
    ring = __ioremap(RING_PHYS, SLOT_BYTES * RING_SLOTS, mapping);
    if (!ring)
        return -ENOMEM;
    int result = misc_register(&mat_device);
    if (result)
        iounmap(ring);
    else
        _printk(LOG "ready\n");
    return result;
}

__attribute__((section(".exit.text"))) void cleanup_module(void)
{
    misc_deregister(&mat_device);
    iounmap(ring);
}
