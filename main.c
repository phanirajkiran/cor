#include "common.h"
#include "chrdev_serial.h"
#include "elf.h"
#include "tss.h"
#include "mm.h"
#include "pci.h"

int my_kernel_subroutine() {
  return 0xbeef;
}

unsigned int hello_main();

extern char cor_stage2_init_data;
extern int cor_stage2_init_data_len;

int lawl = 0xdeadbeef;

void console_clear(void);

void cor_panic(const char *msg) {
  cor_printk("\nPANIC: %s\n", msg);
  while(1) {};
}

void cor_dump_page_table(uint64_t *start, int level) {
  for(uint64_t *page_table_entry = start; page_table_entry < start+512; page_table_entry++) {
    if(*page_table_entry != 0) {
      uint64_t *base = (uint64_t *)(*page_table_entry & ~((1<<12)-1));
      if(level == 1) {
        cor_printk("%x: base %x bits ", (uint64_t)page_table_entry, base);
        if(*page_table_entry & (1<<5)) { cor_printk("A"); } else { cor_printk("-"); } // accessed
        if(*page_table_entry & (1<<1)) { cor_printk("W"); } else { cor_printk("-"); } // writable
        if(*page_table_entry & (1<<0)) { cor_printk("P"); } else { cor_printk("-"); } // present
        cor_printk("\n");
      } else {
        if(level < 4)
          cor_dump_page_table(base, level+1);
      }
    }
  }
}

void dummy_isr();

void cor_hitmarker() {
  cor_printk("FIRED!\n");
}

uint64_t syscall_write(uint64_t fd64, uint64_t buf64, size_t count64) {
  // TODO: make sure these are harmless
  int fd = (int)fd64;
  const void *buf = (const void *)buf64;
  size_t count = (size_t)count64;

  cor_printk("write() fd=%x, buf=%p, n=%x:\n", fd, buf, count);
  cor_printk("    | ");
  for(size_t i = 0; i < count; i++) {
    char c = *((char*)buf+i);
    putc(c);
    if(c == '\n' && i < (count-1)) {
      cor_printk("    | ");
    }
  }
  return 0;
}

void syscall_exit(uint64_t ret64) {
  int ret = (int)ret64;
  cor_printk("exit() ret=%x\n", ret);
  cor_panic("init exited");
}

void cor_writec(const char c);
void (*cor_current_writec)(const char c) = cor_writec;

#pragma pack(push, 1)
struct {
  uint16_t limit;
  uint64_t base;
} idtr;
#pragma pack(pop)

void kernel_main(void) {
  if(sizeof(uint8_t) != 1) {
    cor_printk("sizeof(uint8_t) = %d !!", sizeof(uint8_t));
    cor_panic("assertion failure");
  }
  if(sizeof(uint16_t) != 2) {
    cor_printk("sizeof(uint16_t) = %d !!", sizeof(uint16_t));
    cor_panic("assertion failure");
  }
  if(sizeof(uint32_t) != 4) {
    cor_printk("sizeof(uint32_t) = %d !!", sizeof(uint32_t));
    cor_panic("assertion failure");
  }
  if(sizeof(uint64_t) != 8) {
    cor_printk("sizeof(uint64_t) = %d !!", sizeof(uint64_t));
    cor_panic("assertion failure");
  }

  console_clear();
  uint32_t revision = *((uint32_t*)0x6fffa);
  cor_printk("\n   Cor rev. %xx\n\n",revision);
  cor_printk("Hello from the kernel.\nYes, we can multiline.\n");
  cor_printk("Leet is \'%u\', 0 is \'%u\'\n", 1337, 0);
  cor_printk("Haxx is \'%x\', 0 is \'%x\'\n", 0x2003, 0);

  cor_chrdev_serial_init();
  cor_printk("Switching to serial...\n");
  cor_current_writec = cor_chrdev_serial_write;
  cor_printk("Switched to serial console.\n");

  cor_printk("Initializing MM.. ");
  mm_init();
  cor_printk("OK.\n");

  cor_printk("Initializing PCI.. ");
  pci_init();
  cor_printk("OK.\n");

  cor_printk("Initializing interrupts..");

  // cf. intel_64_software_developers_manual.pdf pg. 1832
  void *target = (void*)(((ptr_t)&dummy_isr) | 0x0000008000000000);

  cor_printk("ISR target is %p\n", target);

  void *base = (void*)(0x6000|0x0000008000000000);
  const int entrysize = 16; // in bytes
  const int n_entry = 55;
  idtr.base = (uint64_t)base;
  idtr.limit = entrysize * n_entry;

  for(int i = 0; i < n_entry; i++) {
    void *offset = base+(i*entrysize);

    *(uint16_t*)(offset+0) = (uint16_t) ((uint64_t)target >> 0);
    *(uint16_t*)(offset+2) = (uint16_t) 8; // segment
    *(uint16_t*)(offset+4) = (uint16_t) 0xee00; // flags
    *(uint16_t*)(offset+6) = (uint16_t) ((uint64_t)target >> 16);
    *(uint32_t*)(offset+8) = (uint32_t) ((uint64_t)target >> 32);
    *(uint32_t*)(offset+12) = (uint32_t) 0; // reserved
  }


  void *x = (void*)&idtr;

  __asm__ (
    "lidt (%0)"
    : : "p" (x)
  );

  int a = 1337;

  cor_printk("done.\n");

  cor_printk("Firing test interrupt.. ");
  __asm__ ( "int $49" );
  cor_printk("returned.. ");
  if(a == 1337) {
    cor_printk("OK, stack looks intact.\n");
  } else {
    cor_panic("Test interrupt seems to have messed up the stack.");
  }

  cor_printk("Setting up TSS.. ");
  tss_setup();
  cor_printk("OK.\n");


  unsigned int rr = hello_main();
  cor_printk("Rust returned: %u\n", rr);
  if(rr != 1337) {
    cor_panic("Rust failed to return magic.\n");
  }

  //cor_panic("ok");

  cor_printk("Exec'ing init.\n");

  // FIXME PLS
  unsigned long *printtarget = (void*)0x55000;
  *printtarget = (unsigned long)cor_printk;

  //cor_dump_page_table((uint64_t *)0x1000, 1);
  cor_elf_exec(&cor_stage2_init_data, cor_stage2_init_data_len);

  while(1) {
    cor_printk(".");
    for(int i = 0; i < 1000000; i++);
  }
}

const int console_width = 80;
const int console_height = 25;
int console_line = 0;
int console_col = 0;

void console_clear(void) {
  console_line = 0;
  console_col = 0;
  for(int i = 0; i < (console_width*console_height); i++)
    *((unsigned char*)0xB8000+i*2) = ' ';
}

void cor_writec(const char c) {
  if(console_col == console_width-1 || c == '\n') {
    console_line++;
    console_col = 0;
    if(c == '\n') return;
  }

  int grid_index = (console_col++) + console_line*console_width;

  *((unsigned char*)0xB8000+grid_index*2) = c;
}
