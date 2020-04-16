SECTIONS {
    .text : {
	. = ALIGN(4);
	KEEP(*(.fwhdr))
	. = ALIGN(4);
	*(.text)
	*(.text*)
	. = ALIGN(4);
    } >FWFLASH

    .data : {
	. = ALIGN(4);
	_sdata = .;
	*(.data)
	*(.data*)
	. = ALIGN(4);
	_edata = .;
    } >RAM AT>FWFLASH

    _sidata = LOADADDR(.data);
  
    .bss : {
	. = ALIGN(4);
	_sbss = .;
	*(.bss)
	*(.bss*)
	*(COMMON)
	. = ALIGN(4);
	_ebss = .;
    } >RAM

    .rodata : {
	. = ALIGN(4);
	*(.rodata)
	*(.rodata*)
	. = ALIGN(4);

	/* make sure flash image is a multiple of page size */
	FILL(0x00000000)
	. = ALIGN(128);
	__fw_end__ = .;
    } >FWFLASH

    /DISCARD/ : {
	*(.ARM)
	*(.ARM*)
    }
}
