all: listfs-tool
listfs-tool: listfs-tool.c listfs.h
	gcc `pkg-config --libs --cflags fuse` -O2 -o listfs-tool listfs-tool.c
