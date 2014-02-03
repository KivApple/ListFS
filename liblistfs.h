#ifndef LIBLISTFS_H
#define LIBLISTFS_H

#include <stdarg.h>
#include <stdbool.h>
#include "listfs.h"

typedef struct _ListFS ListFS;
struct _ListFS {
	void (*read_block_func)(ListFS*, uint64_t, void*);
	void (*write_block_func)(ListFS*, uint64_t, void*);
	void (*log_func)(ListFS*, char *fmt, va_list args);
	ListFS_Header *header;
	uint8_t *map;
	uint64_t last_allocated_block;
};

typedef struct {
	ListFS *fs;
	uint64_t node;
	ListFS_NodeHeader *node_header;
	uint64_t cur_global_offset;
	uint64_t cur_block_list_block;
	uint64_t *cur_block_list;
	uint32_t cur_block;
	uint32_t cur_offset;
	uint32_t link_count;
} ListFS_OpennedFile;

ListFS *listfs_init(void (*read_block_func)(ListFS*, uint64_t, void*),
	void (*write_block_func)(ListFS*, uint64_t, void*), void (*log_func)(ListFS*, char*, va_list));
void listfs_create(ListFS *this, uint64_t size, uint16_t block_size, void *bootloader, size_t bootloader_size);
bool listfs_open(ListFS *this);
void listfs_close(ListFS *this);

uint64_t listfs_create_node(ListFS *this, uint8_t *name, uint32_t flags, uint64_t parent);
bool listfs_delete_node(ListFS *this, uint64_t node);
void listfs_move_node(ListFS *this, uint64_t node, uint64_t new_parent);
void listfs_foreach_node(ListFS *this, uint64_t node, bool (*callback)(ListFS*, uint64_t, ListFS_NodeHeader*, void*), void *data);
void listfs_foreach_subnode(ListFS *this, uint64_t node, bool (*callback)(ListFS*, uint64_t, ListFS_NodeHeader*, void*), void *data);
uint64_t listfs_search_node(ListFS *this, uint8_t *path, uint64_t first);
ListFS_NodeHeader *listfs_fetch_node(ListFS *this, uint64_t node);
void listfs_rename_node(ListFS *this, uint64_t node, uint8_t *name);

ListFS_OpennedFile *listfs_open_file(ListFS *this, uint64_t node);
void listfs_file_close(ListFS_OpennedFile *this);
void listfs_file_seek(ListFS_OpennedFile *this, uint64_t offset, bool write);
void listfs_file_truncate(ListFS_OpennedFile *this);
size_t listfs_file_write(ListFS_OpennedFile *this, void *buffer, size_t length);
size_t listfs_file_read(ListFS_OpennedFile *this, void *buffer, size_t length);

#endif