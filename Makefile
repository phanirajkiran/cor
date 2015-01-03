.PHONY: all clean
all: disk.bin
# TODO: add a real configure script to remove debug options
CFLAGS=-nostdlib -nostdinc -static -nostartfiles -nodefaultlibs -Wall -Wextra -m64 -Werror -std=c11 -ggdb
CC=./sshwrap gcc
OBJCOPY=./sshwrap objcopy
LD=./sshwrap ld
AS=./sshwrap as
OBJS=main.o printk.o chrdev_serial.o io.o elf.o interrupthandler.o tss.o

clean:
	rm -f *.o *.bin *~ init *.so

boot.o: boot.s
	$(AS) boot.s -o boot.o

mbr.bin: blank_mbr boot.o
	cp blank_mbr mbr.bin
	$(OBJCOPY) --only-section=.text -O binary boot.o mbr_contents.bin
	dd if=mbr_contents.bin of=mbr.bin conv=notrunc
	rm mbr_contents.bin

stage2_entrypoint.o: stage2_entrypoint.s
	$(CC) $(CFLAGS) -c stage2_entrypoint.s -o stage2_entrypoint.o

interrupthandler.o: interrupthandler.s
	$(CC) $(CFLAGS) -c $< -o $@

stage2.o: $(OBJS) linkerscript stage2_entrypoint.o init_static.o
	echo LONG\(0x$(shell git rev-parse HEAD | cut -c 1-6)\) > versionstamp~
	$(LD) $(OBJS) stage2_entrypoint.o init_static.o -T linkerscript -o stage2.o

stage2.bin: stage2.o
	$(OBJCOPY) --only-section=.text -O binary stage2.o stage2.bin

disk.bin: mbr.bin stage2.o Makefile
	# we want a 0x60100 byte disk
	dd if=/dev/zero of=disk.bin~ conv=notrunc bs=1 count=$$((0x60100))
	$(OBJCOPY) -I elf64-x86-64 --only-section=.text -O binary stage2.o stage2_text~
	$(OBJCOPY) -I elf64-x86-64 --only-section=.data -O binary stage2.o stage2_data~
	# add the MBR.
	dd if=mbr.bin of=disk.bin~ conv=notrunc
	# add the text section
	dd if=stage2_text~ of=disk.bin~ conv=notrunc bs=1 seek=512
	# and add the data. ultra hacky to mess with the offsets here, and it's probably wrong, but meh...
	dd if=stage2_data~ of=disk.bin~ conv=notrunc bs=1 seek=$$((512 + 0x40000))
	mv disk.bin~ disk.bin

init_static.o: init_static.c~
	$(CC) $(CFLAGS) -c -x c $< -o $@

init_static.c~: init
	cat $< | ruby -e 'b = $$stdin.read.bytes; puts "int cor_stage2_init_data_len = "+b.count.to_s+"; char cor_stage2_init_data[] = {";puts b.map{|x|"0x#{x.to_s(16)}"}.join(", ");puts "};"' > $@

init: init.c
	$(CC) $(CFLAGS) -Iinclude -fPIC init.c -o init
