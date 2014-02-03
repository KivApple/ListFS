CFLAGS=-O2

all: liblistfs.so listfs-tool boot.bios.bin
liblistfs.so: liblistfs.c liblistfs.h listfs.h
	gcc $(CFLAGS) -fPIC -shared -Wl,-soname,liblistfs.so.0 -o liblistfs.so.0 liblistfs.c
	ln -sf liblistfs.so.0 liblistfs.so
listfs-tool: listfs-tool.c liblistfs.h liblistfs.so
	gcc $(CFLAGS) `pkg-config --cflags --libs fuse` -L. -llistfs -o listfs-tool listfs-tool.c
boot.bios.bin: boot.bios.asm
	fasm boot.bios.asm boot.bios.bin
clean:
	rm -f *.so *.so.0 listfs-tool
install:
	install -m 0755 liblistfs.so.0 /usr/lib
	install -m 0755 liblistfs.so /usr/lib
	install -m 0755 listfs-tool /usr/bin
uninstall:
	rm -f /usr/lib/liblistfs.so
	rm -f /usr/lib/liblistfs.so.0
	rm -f /usr/bin/listfs-tool