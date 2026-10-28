/* STM32H7S3L8: internal 64 KiB Flash, AXI SRAM DMA, DTCM stack. MIT. */
ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (rwx) : ORIGIN = 0x24000000, LENGTH = 456K
  DTCM (rw) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(DTCM) + LENGTH(DTCM);
PHDRS { flash PT_LOAD FLAGS(5); ram PT_LOAD FLAGS(6); bss PT_LOAD FLAGS(6); stack PT_LOAD FLAGS(6); }
SECTIONS {
 .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) } > FLASH :flash
 .text : { *(.text*) *(.glue_7*) *(.eh_frame*) KEEP(*(.init)) KEEP(*(.fini)) } > FLASH :flash
 .rodata : { *(.rodata*) . = ALIGN(4); } > FLASH :flash
 .ARM.extab : { *(.ARM.extab*) } > FLASH :flash
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH :flash
 .preinit_array (READONLY) : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH :flash
 .init_array (READONLY) : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH :flash
 .fini_array (READONLY) : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH :flash
 _sidata = LOADADDR(.data);
 .data : { . = ALIGN(32); _sdata = .; *(.data*) *(.RamFunc*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH :ram
 .bss (NOLOAD) : { . = ALIGN(32); _sbss = .; __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(32); _ebss = .; __bss_end__ = .; } > RAM :bss
 .stack ORIGIN(DTCM) + LENGTH(DTCM) - 8192 (NOLOAD) : { _stack_bottom = .; . += 8192; } > DTCM :stack
 ASSERT(_ebss <= ORIGIN(RAM) + LENGTH(RAM), "AXI SRAM overflow")
 ASSERT(SIZEOF(.stack) <= LENGTH(DTCM), "DTCM stack overflow")
 /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) }
 .ARM.attributes 0 : { *(.ARM.attributes) }
}
