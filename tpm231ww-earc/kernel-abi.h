/* Field offsets verified from the running Philips kernel's BTF. */
struct module {
    u8 before_name[0x18];
    char name[56];
    u8 before_init[0x178 - 0x18 - 56];
    int (*init)(void);
    u8 before_exit[0x3d0 - 0x178 - 8];
    void (*exit)(void);
    u8 remaining[0x400 - 0x3d0 - 8];
};

int init_module(void);
void cleanup_module(void);

__attribute__((section(".gnu.linkonce.this_module"), aligned(64), used))
struct module __this_module = {
    .name = "philips_earc_mat", .init = init_module, .exit = cleanup_module,
};
__attribute__((section(".init.data"), used))
const void *__cfi_jt_init_module = (void *)&init_module;
__attribute__((section(".exit.data"), used))
const void *__cfi_jt_cleanup_module = (void *)&cleanup_module;

struct file_operations {
    struct module *owner;
    u8 before_read[8];
    ssize_t (*read)(struct file *, char *, size_t, loff_t *);
    u8 before_write_iter[0x28 - 0x18];
    ssize_t (*write_iter)(struct kiocb *, struct iov_iter *);
    u8 before_open[0x70 - 0x30];
    int (*open)(struct inode *, struct file *);
    u8 before_release[8];
    int (*release)(struct inode *, struct file *);
    int (*fsync)(struct file *, loff_t, loff_t, int);
    u8 remaining[0x120 - 0x90];
};

static const struct file_operations operations = {
    .owner = &__this_module, .open = dev_open, .release = dev_release,
    .read = dev_read, .write_iter = dev_write, .fsync = dev_fsync,
};

struct miscdevice {
    int minor;
    const char *name;
    const struct file_operations *fops;
    u8 before_mode[0x48 - 0x18];
    u16 mode;
};

extern int misc_register(struct miscdevice *);
extern void misc_deregister(struct miscdevice *);

static struct miscdevice mat_device = {
    .minor = 255, .name = "earc_mat_bypass", .fops = &operations, .mode = 0660,
};

_Static_assert(sizeof(struct module) == 0x400, "struct module size");
_Static_assert(__builtin_offsetof(struct module, init) == 0x178, "struct module init");
_Static_assert(__builtin_offsetof(struct module, exit) == 0x3d0, "struct module exit");
_Static_assert(sizeof(struct file_operations) == 0x120, "file operations size");
_Static_assert(__builtin_offsetof(struct file_operations, read) == 0x10, "read offset");
_Static_assert(__builtin_offsetof(struct file_operations, write_iter) == 0x28, "write_iter offset");
_Static_assert(__builtin_offsetof(struct file_operations, open) == 0x70, "open offset");
_Static_assert(__builtin_offsetof(struct file_operations, fsync) == 0x88, "fsync offset");
_Static_assert(sizeof(struct miscdevice) == 0x50, "miscdevice size");
_Static_assert(__builtin_offsetof(struct miscdevice, mode) == 0x48, "miscdevice mode offset");
_Static_assert(sizeof(struct earc_mat_status) == 40, "status ABI size");

struct modversion_info { u64 crc; char name[56]; };
__attribute__((section("__versions"), used))
static const struct modversion_info versions[] = {
#include "versions.inc"
};

#define MODINFO __attribute__((section(".modinfo"), aligned(1), used))
MODINFO static const char license[] = "license=GPL";
MODINFO static const char module_name[] = "name=philips_earc_mat";
MODINFO static const char depends[] = "depends=mtk_tvsnd_soc";
MODINFO static const char vermagic[] = "vermagic=5.15.192-android14-11-00092-g9a68f81c71fb-dirty SMP preempt mod_unload modversions aarch64";
