#ifndef LISTFS_H
#define LISTFS_H

#include <stdint.h>

#define LISTFS_VERSION_MAJOR 1
#define LISTFS_VERSION_MINOR 0

#define LISTFS_MAGIC 0x5453494C
#define LISTFS_MIN_BLOCK_SIZE 512

typedef uint64_t ListFS_BlockIndex;
typedef uint64_t ListFS_BlockCount;

typedef struct {
	uint8_t jump[4];
	uint32_t magic;
	ListFS_BlockIndex base;
	ListFS_BlockCount size;
	ListFS_BlockIndex map_base;
	ListFS_BlockCount map_size;
	ListFS_BlockIndex root_dir;
	uint16_t block_size;
	uint16_t version;
	ListFS_BlockCount used_blocks;
} __attribute__((packed)) ListFS_Header;

#define LISTFS_NODE_MAGIC 0x45444F4E
#define LISTFS_NODE_FLAG_DIRECTORY 1

typedef struct {
	uint8_t name[256];
	ListFS_BlockIndex parent;
	ListFS_BlockIndex next;
	ListFS_BlockIndex prev;
	ListFS_BlockIndex data;
	uint32_t magic;
	uint32_t flags;
	uint64_t size;
	uint64_t create_time;
	uint64_t modify_time;
	uint64_t access_time;
} __attribute__((packed)) ListFS_NodeHeader;

#endif