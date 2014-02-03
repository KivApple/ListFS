#ifndef LISTFS_H
#define LISTFS_H

#include <stdint.h>

#define LISTFS_VERSION_MAJOR 1
#define LISTFS_VERSION_MINOR 0

#define LISTFS_MAGIC 0x5453494C
#define LISTFS_MIN_BLOCK_SIZE 512

typedef struct {
	uint8_t jump[4];
	uint32_t magic;
	uint64_t base;
	uint64_t size;
	uint64_t map_base;
	uint64_t map_size;
	uint64_t root_dir;
	uint16_t block_size;
	uint16_t version;
} ListFS_Header;

#define LISTFS_NODE_MAGIC 0x45444F4E
#define LISTFS_NODE_FLAG_DIRECTORY 1

typedef struct {
	uint8_t name[256];
	uint64_t parent;
	uint64_t next;
	uint64_t prev;
	uint64_t data;
	uint32_t magic;
	uint32_t flags;
	uint64_t size;
} ListFS_NodeHeader;

#endif
