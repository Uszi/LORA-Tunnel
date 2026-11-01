/* l2ram: range 0x4FF0'0000 - 0x4FFC'0000
   in use by rom range: 0x4FFA'96B8 - 0x4FFC'0000
 */
MEMORY {
  iram : org = 0x4FF50000, len = 0x10000
  dram : org = 0x4FF60000, len = 0x20000
}

ENTRY(stub_main)

SECTIONS {
  .text : ALIGN(4) {
    *(.literal)
    *(.text .text.*)
  } > iram

  .bss : ALIGN(4) {
    _bss_start = ABSOLUTE(.);
    *(.bss)
    _bss_end = ABSOLUTE(.);
  } > dram

  .data : ALIGN(4) {
    *(.data)
    *(.rodata .rodata.*)
  } > dram
}

INCLUDE "rom_32p4.ld"
