/* The QEMU image's memory, laid out as the XMC4500's budget is (target/xmc4500/xmc4500.ld): 1 MB
 * of flash, and two RAM regions of 64 KB, each its own MEMORY region so that no object can span
 * them. On mps2-an386 the flash is at QEMU's code memory, 0x00000000, and both RAM regions are in
 * its SRAM at 0x20000000.
 *   - PSRAM: the main stack, which exceptions and main before the scheduler run on;
 *   - DSRAM1: .data, and .bss with the actor host's memory, .bss.shell, placed first by name. */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 1M
    PSRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
    DSRAM1 (rwx) : ORIGIN = 0x20010000, LENGTH = 64K
}

/* The main stack's size, which an image may set with --defsym: the firmware's is its interrupts'
 * budget and room for main; the test image's runs every test. */
PROVIDE(__main_stack_size = 2048);

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > FLASH

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .main_stack (NOLOAD) :
    {
        . = ALIGN(8);
        . += __main_stack_size;
        . = ALIGN(8);
        __stack_top = .;
    } > PSRAM

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > DSRAM1 AT > FLASH
    __data_load = LOADADDR(.data);

    /* The actor host's memory (port_memory.h), and then the rest of .bss: both zeroed, from
     * __bss_start to __bss_end. */
    .shell (NOLOAD) :
    {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss.shell)
    } > DSRAM1

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > DSRAM1

    /* The test image's heap, for the host tests' std::vector; the firmware has none. */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        *(.heap .heap.*)
    } > DSRAM1
}
