CFLAGS+=-O2

all: liblistfs.so listfs-tool bootloaders/boot.bios.bin
liblistfs.so: liblistfs.c liblistfs.h listfs.h
	gcc $(CFLAGS) -fPIC -shared -Wl,-soname,liblistfs.so.0 -o liblistfs.so.0 liblistfs.c
	ln -sf liblistfs.so.0 liblistfs.so
listfs-tool: listfs-tool.c liblistfs.h liblistfs.so
	gcc $(CFLAGS) `pkg-config --cflags --libs fuse` -L. -llistfs -o listfs-tool listfs-tool.c
bootloaders/boot.bios.bin: bootloaders/boot.bios.asm
	fasm bootloaders/boot.bios.asm bootloaders/boot.bios.bin
clean:
	rm -f liblistfs.so liblistfs.so.0 listfs-tool bootloaders/boot.bios.bin
install:
	install -m 0755 liblistfs.so.0 /usr/lib
	install -m 0755 liblistfs.so /usr/lib
	install -m 0755 listfs-tool /usr/bin
	install -m 0644 listfs.h /usr/include
uninstall:
	rm -f /usr/lib/liblistfs.so
	rm -f /usr/lib/liblistfs.so.0
	rm -f /usr/bin/listfs-tool
	rm -f /usr/include/listfs.h