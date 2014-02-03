ListFS
======
ListFS is simple and straightforward file system, which is designed for the study of the computer and write their own operating systems.

This file system uses wherever possible, two-way linked list.
To date, the FS does not have many features of modern systems (no access rights, file attributes, journal),
but this does not mean that it can not be added in the future.

Compilation and installation
------
* make - Build default configuration
* CFLAGS=-DDISABLE_FUSE make - Disable FUSE support by listfs-tool
* CFLAGS=-DDISABLE_TIME make - Disable timestamp support by liblistfs
* make clean - Remove compiled files
* sudo make install - Install liblistfs and listfs-tool to the system

Build dependencies:
* libc (listfs-tool and liblistfs)
* fuse (listfs-tool)
* fasm (BIOS bootloader)

Licence
------
Programs and libraries distributed under GNU LGPL Licence

Copyright (C) kiv <kiv.apple@gmail.com>

Data structures
------

### ListFS header

* uint8_t jump[4] - reserved
* uint32_t magic - "LIST"
* uint64_t base - base block index
* uint64_t size - size of file system in blocks
* uint64_t map_base - base of FS bitmap
* uint64_t map_size - size of FS bitmap
* uint64_t root_dir - first node in root directory
* uint16_t block_size - block size
* uint16_t version - version (0xHHLL)


### ListFS node header

* uint8_t name[256] - name of node
* uint64_t parent - parent node (-1 if node placed in root directory)
* uint64_t next - next node (-1 if this is last node in directory)
* uint64_t prev - prev node (-1 if this is first node in directory)
* uint64_t data - first node in directory or first file block list (maybe -1)
* uint32_t magic - "NODE"
* uint32_t flags - flags (1 - this is directory)
* uint64_t size - size in bytes
* uint64_t create_time
* uint64_t modify_time
* uint64_t access_time


### ListFS file block list

* uint64_t prev_list - Prev list (-1 if this is first list)
* uint64_t blocks[] - Blocks
* uint64_t next_list - Next list (-1 if this is last list)