SECTIONS
{
    .plt (NOLOAD) : { BYTE(0) }
    .init.plt (NOLOAD) : { BYTE(0) }
    .text.ftrace_trampoline (NOLOAD) : { BYTE(0) }
    .text : ALIGN(4096) { *(.text.__cfi_check) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
}
