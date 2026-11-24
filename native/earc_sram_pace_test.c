/*
 * Exact 20ms kernel-paced SRAM 0x1113 advance test for V670 C9K.
 * Uses exported MApi_AUDIO_offload_Debug_Cmd_Write (mutex-safe) to advance
 * SE DSP DM SRAM 0x1113 by 0x0f00 (61,440 / 16 = 1 MAT frame) every 20 ms
 * modulo 0x1a400 (28 MAT frames) for 3 seconds, then auto-unloads (-EAGAIN).
 */
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

extern int _printk(const char *fmt, ...);
extern void msleep(unsigned int msecs);
extern u8 MApi_AUDIO_offload_Debug_Cmd_Write(char *cmd, u32 len);

__attribute__((section(".plt"), used)) void __plt_stub(void) {}
__attribute__((section(".init.plt"), used)) void __init_plt_stub(void) {}
__attribute__((section(".text.ftrace_trampoline"), used)) void __ftrace_trampoline_stub(void) {}
__attribute__((aligned(4096), used)) void __cfi_check(u64 id, void *ptr, void *diag) { (void)id; (void)ptr; (void)diag; }

static void fmt_cmd(char *buf, u32 val)
{
    static const char prefix[] = "write_dsp_sram_type=1 addr=0x1113 value=0x";
    static const char hex[] = "0123456789abcdef";
    int k = 0;
    for (int i = 0; prefix[i]; i++) buf[k++] = prefix[i];
    for (int shift = 20; shift >= 0; shift -= 4)
        buf[k++] = hex[(val >> shift) & 0xf];
    buf[k++] = '\n';
    buf[k] = 0;
}

int init_module(void)
{
    char cmd[64];
    u32 wptr = 0x0780; /* aligned to HIT#0 @ +0x7800 */
    _printk("\0016[earc_sram_pace] START: advancing SRAM 0x1113 by 0x0f00 every 20ms for 3s...\n");
    for (int i = 0; i < 150; i++) {
        wptr += 0x0f00;
        if (wptr >= 0x1a400) wptr -= 0x1a400;
        fmt_cmd(cmd, wptr);
        MApi_AUDIO_offload_Debug_Cmd_Write(cmd, 64);
        msleep(20);
    }
    _printk("\0016[earc_sram_pace] DONE: final wptr=0x%x, auto-unloading.\n", wptr);
    return -11; /* -EAGAIN */
}

void cleanup_module(void) {}

struct v670_module { u8 p0[0x18]; char name[56]; u8 p1[0x178-0x50]; int (*init)(void); u8 p2[0x378-0x180]; void (*exit)(void); u8 p3[0x40]; };
__attribute__((section(".gnu.linkonce.this_module"), used)) struct v670_module __this_module = { .name = "earc_sram_pace_test", .init = init_module, .exit = cleanup_module };
struct modversion_info { u64 crc; char name[56]; };
__attribute__((section("__versions"), used, aligned(8))) static const struct modversion_info ____versions[] = {
    { 0xb82e8f7bULL, "module_layout" },
    { 0x92997ed8ULL, "_printk" },
    { 0xf9a482f9ULL, "msleep" },
    { 0x8b1cd891ULL, "MApi_AUDIO_offload_Debug_Cmd_Write" },
};
__attribute__((section(".modinfo"), used, aligned(1))) static const char m1[] = "license=GPL v2";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m2[] = "vermagic=5.15.192-android14-11 SMP preempt mod_unload modversions aarch64";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m3[] = "name=earc_sram_pace_test";
__attribute__((section(".modinfo"), used, aligned(1))) static const char m4[] = "depends=mtk-tvsnd-soc";
