#ifndef LISTFS_H
#define LISTFS_H

#include <stdint.h>

#define LISTFS_MAGIC 0x84837376
#define LISTFS_VERSION_MAJOR 1
#define LISTFS_VERSION_MINOR 0
#define LISTFS_VERSION ((LISTFS_VERSION_MAJOR << 8) | LISTFS_VERSION_MINOR)

#define LISTFS_BLOCK_SIZE 512

typedef struct {
	uint8_t reserved[4];
	uint32_t magic;
	uint32_t version;
	uint32_t attrs;
	uint64_t base;
	uint64_t size;
	uint64_t map_base;
	uint64_t map_size;
	uint64_t first_file;
	uint64_t uid;
	uint32_t block_size;
} __attribute__((packed)) listfs_header;

#define LISTFS_MAX_FILE_NAME 256

#define LISTFS_FILE_ATTR_DIR 1

typedef struct {
	uint8_t name[LISTFS_MAX_FILE_NAME];
	uint64_t next;
	uint64_t prev;
	uint64_t parent;
	uint64_t attrs;
	uint64_t data;
	uint64_t size;
	uint64_t create_time;
	uint64_t modify_time;
	uint64_t access_time;
} __attribute__((packed)) listfs_file_header;

typedef struct {
	uint64_t blocks[LISTFS_BLOCK_SIZE / sizeof(uint64_t) - 1];
	uint64_t next_list;
} __attribute__((packed)) listfs_block_list;

#endif
