#ifndef LIBLISTFS_H
#define LIBLISTFS_H

#include <stdarg.h>
#include <stdbool.h>
#include "listfs.h"

typedef struct _ListFS ListFS;
struct _ListFS {
	void (*read_block_func)(ListFS*, ListFS_BlockIndex, void*);
	void (*write_block_func)(ListFS*, ListFS_BlockIndex, void*);
	void (*log_func)(ListFS*, char *fmt, va_list args);
	ListFS_Header *header;
	uint8_t *map;
	ListFS_BlockIndex last_allocated_block;
};

typedef struct {
	ListFS *fs;
	ListFS_BlockIndex node;
	ListFS_NodeHeader *node_header;
	uint64_t cur_global_offset;
	ListFS_BlockIndex cur_block_list_block;
	ListFS_BlockIndex *cur_block_list;
	uint32_t cur_block;
	uint32_t cur_offset;
	unsigned int link_count;
} ListFS_OpennedFile;

ListFS *listfs_init(void (*read_block_func)(ListFS*, ListFS_BlockIndex, void*),
	void (*write_block_func)(ListFS*, ListFS_BlockIndex, void*), void (*log_func)(ListFS*, char*, va_list));
void listfs_create(ListFS *this, ListFS_BlockCount size, uint16_t block_size, void *bootloader, size_t bootloader_size);
bool listfs_open(ListFS *this);
void listfs_close(ListFS *this);

ListFS_BlockIndex listfs_create_node(ListFS *this, uint8_t *name, uint32_t flags, ListFS_BlockIndex parent);
bool listfs_delete_node(ListFS *this, ListFS_BlockIndex node);
void listfs_move_node(ListFS *this, ListFS_BlockIndex node, ListFS_BlockIndex new_parent);
void listfs_foreach_node(ListFS *this, ListFS_BlockIndex node, bool (*callback)(ListFS*, ListFS_BlockIndex, ListFS_NodeHeader*, void*), void *data);
void listfs_foreach_subnode(ListFS *this, ListFS_BlockIndex node, bool (*callback)(ListFS*, ListFS_BlockIndex, ListFS_NodeHeader*, void*), void *data);
ListFS_BlockIndex listfs_search_node(ListFS *this, uint8_t *path, ListFS_BlockIndex first);
ListFS_NodeHeader *listfs_fetch_node(ListFS *this, ListFS_BlockIndex node);
void listfs_rename_node(ListFS *this, ListFS_BlockIndex node, uint8_t *name);

ListFS_OpennedFile *listfs_open_file(ListFS *this, ListFS_BlockIndex node);
void listfs_file_close(ListFS_OpennedFile *this);
void listfs_file_seek(ListFS_OpennedFile *this, uint64_t offset, bool write);
void listfs_file_truncate(ListFS_OpennedFile *this);
size_t listfs_file_write(ListFS_OpennedFile *this, void *buffer, size_t length);
size_t listfs_file_read(ListFS_OpennedFile *this, void *buffer, size_t length);

#endif