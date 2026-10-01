/* The XMC4500-F144x1024's memory, for the firmware image. Modelled on Infineon's XMC4500x1024.ld
 * (mtb-xmclib-cat3 4.7.0), whose startup_XMC4500.S this image links: it keeps the symbols that
 * startup copies and zeroes by, and the .no_init block system_XMC4500.c keeps its clock and chip
 * id in, and it drops Infineon's heap banks, since this firmware has no heap.
 *   - FLASH: 1 MB, run cached at 0x08000000 and loaded at its uncached alias, 0x0C000000;
 *   - PSRAM_1: 64 KB, the main stack, which exceptions and main before the scheduler run on;
 *   - DSRAM_1_system: 64 KB, .data, and .bss with the actor host's memory, .bss.shell, placed
 *     first by name; the last 64 bytes are .no_init;
 *   - DSRAM_2_comm: 32 KB, for Ethernet and USB buffers, unused here.
 * Each RAM region is its own MEMORY region, so no object can span two: the link fails first. */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(Reset_Handler)

MEMORY
{
    FLASH_1_cached (RX) : ORIGIN = 0x08000000, LENGTH = 0x100000
    FLASH_1_uncached (RX) : ORIGIN = 0x0C000000, LENGTH = 0x100000
    PSRAM_1 (!RX) : ORIGIN = 0x10000000, LENGTH = 0x10000
    DSRAM_1_system (!RX) : ORIGIN = 0x20000000, LENGTH = 0x10000
    DSRAM_2_comm (!RX) : ORIGIN = 0x30000000, LENGTH = 0x8000
}

/* The main stack: the interrupts' budget, nested, and main's own before the scheduler starts. */
stack_size = DEFINED(stack_size) ? stack_size : 2048;
no_init_size = 64;

SECTIONS
{
    .text :
    {
        sText = .;
        KEEP(*(.reset));
        *(.text .text.* .gnu.linkonce.t.*);
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r*)
        . = ALIGN(4);
    } > FLASH_1_cached AT > FLASH_1_uncached

    .ARM.extab : ALIGN (4)
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH_1_cached AT > FLASH_1_uncached

    . = ALIGN(4);
    __exidx_start = .;
    .ARM.exidx : ALIGN (4)
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH_1_cached AT > FLASH_1_uncached
    __exidx_end = .;

    Stack (NOLOAD) :
    {
        __stack_start = .;
        . = . + stack_size;
        __stack_end = .;
        __initial_sp = .;
    } > PSRAM_1

    /* The startup's other copy and zero entries: empty here, and kept for it. */
    .ram_code :
    {
        . = ALIGN(4);
        __ram_code_start = .;
        *(.ram_code)
        . = ALIGN(4);
        __ram_code_end = .;
    } > PSRAM_1 AT > FLASH_1_uncached
    __ram_code_load = LOADADDR (.ram_code);
    __ram_code_size = __ram_code_end - __ram_code_start;

    PSRAM_DATA :
    {
        . = ALIGN(4);
        __data3_start = .;
        *(PSRAM_DATA)
        . = ALIGN(4);
        __data3_end = .;
    } > PSRAM_1 AT > FLASH_1_uncached
    __data3_load = LOADADDR (PSRAM_DATA);
    __data3_size = __data3_end - __data3_start;

    PSRAM_BSS (NOLOAD) :
    {
        . = ALIGN(4);
        __bss3_start = .;
        *(PSRAM_BSS)
        . = ALIGN(4);
        __bss3_end = .;
    } > PSRAM_1
    __bss3_size = __bss3_end - __bss3_start;

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data .data.*)
        *(.gnu.linkonce.d*)
        . = ALIGN(4);
        __data_end = .;
    } > DSRAM_1_system AT > FLASH_1_uncached
    __data_load = LOADADDR (.data);
    __data_size = __data_end - __data_start;

    /* The actor host's memory (port_memory.h), and then the rest of .bss: both zeroed, from
     * __bss_start for __bss_size. */
    .shell (NOLOAD) :
    {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss.shell)
    } > DSRAM_1_system

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        *(.bss .bss.* COMMON)
        *(.gnu.linkonce.b*)
        . = ALIGN(4);
        __bss_end = .;
    } > DSRAM_1_system
    __bss_size = __bss_end - __bss_start;

    /* system_XMC4500.c's chip id, SystemCoreClock and trimming data, which survive a reset. */
    .no_init ORIGIN(DSRAM_1_system) + LENGTH(DSRAM_1_system) - no_init_size (NOLOAD) :
    {
        __no_init_start = .;
        *(.no_init);
    } > DSRAM_1_system
    ASSERT(__bss_end <= __no_init_start, "DSRAM_1_system's .bss overflows into .no_init")

    DSRAM2_DATA :
    {
        . = ALIGN(4);
        __data2_start = .;
        *(DSRAM2_DATA)
        . = ALIGN(4);
        __data2_end = .;
    } > DSRAM_2_comm AT > FLASH_1_uncached
    __data2_load = LOADADDR(DSRAM2_DATA);
    __data2_size = __data2_end - __data2_start;

    DSRAM2_BSS (NOLOAD) :
    {
        . = ALIGN(4);
        __bss2_start = .;
        *(ETH_RAM)
        *(USB_RAM)
        *(DSRAM2_BSS)
        . = ALIGN(4);
        __bss2_end = .;
    } > DSRAM_2_comm
    __bss2_size = __bss2_end - __bss2_start;

    /DISCARD/ : { *(.comment) }
    .ARM.attributes 0 : { KEEP(*(.ARM.attributes)) }
}
