/*
******************************************************************************
**
**  File        : STM32L432XX_APP_BANK_A.ld
**
**  Abstract    : Linker script for STM32L432KCUx dual-bank application Bank A
**                256 Kbytes FLASH and 64 Kbytes RAM
**
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);
/* Generate a link error if heap and stack don't fit into RAM */
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;

/* Specify the memory areas */
MEMORY
{
RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 48K
RAM2 (xrw)      : ORIGIN = 0x10000000, LENGTH = 16K
FLASH (rx)      : ORIGIN = 0x08008000, LENGTH = 104K
}

/* Define output sections */
SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(8);
    KEEP(*(.isr_vector))
    . = ALIGN(8);
  } >FLASH

  .text :
  {
    . = ALIGN(8);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(8);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(8);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(8);
  } >FLASH

  .ARM.extab (READONLY) :
  {
    . = ALIGN(8);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(8);
  } >FLASH

  .ARM (READONLY) :
  {
    . = ALIGN(8);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(8);
  } >FLASH

  .preinit_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(8);
  } >FLASH

  .init_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(8);
  } >FLASH

  .fini_array (READONLY) :
  {
    . = ALIGN(8);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(8);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(8);
    _sdata = .;
    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)

    . = ALIGN(8);
    _edata = .;
  } >RAM AT> FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}
