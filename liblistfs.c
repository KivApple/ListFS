/*
	This file is part of liblistfs.
	Copyright (C) 2014 kiv <kiv.apple@gmail.com>
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#ifndef DISABLE_TIME
#include <time.h>
#endif
#include "listfs.h"
#include "liblistfs.h"

typedef struct {
	ListFS_BlockIndex node;
	ListFS_OpennedFile *file;
} FileInfo;

FileInfo *file_info = NULL;
size_t file_info_count;

/* Some utility functions */

uint64_t min(uint64_t a, uint64_t b) {
	return (a < b) ? a : b;
}

uint64_t max(uint64_t a, uint64_t b) {
	return (a > b) ? a : b;
}

uint64_t bytes_to_blocks(uint64_t bytes, uint16_t block_size) {
	return (bytes + block_size - 1) / block_size;
}

/* I/O functions */

void listfs_log(ListFS *this, char *fmt, ...) {
	if (!this) return;
	if (this->log_func) {
		va_list ap;
		va_start(ap, fmt);
		this->log_func(this, fmt, ap);
		va_end(ap);
	}
}

void listfs_read_block(ListFS *this, ListFS_BlockIndex index, void *buffer) {
	if (!this) return;
	listfs_log(this, "[%s] index = %llu\n", __func__, index);
	this->read_block_func(this, index, buffer);
}

void listfs_read_blocks(ListFS *this, ListFS_BlockIndex index, void *buffer, size_t count) {
	if (!this) return;
	listfs_log(this, "[%s] index = %llu, count = %i\n", __func__, index, count);
	while (count) {
		listfs_read_block(this, index, buffer);
		index++;
		buffer += this->header->block_size;
		count--;
	}
}

void listfs_write_block(ListFS *this, ListFS_BlockIndex index, void *buffer) {
	if (!this) return;
	listfs_log(this, "[%s] index = %llu\n", __func__, index);
	this->write_block_func(this, index, buffer);
}

void listfs_write_blocks(ListFS *this, ListFS_BlockIndex index, void *buffer, size_t count) {
	if (!this) return;
	listfs_log(this, "[%s] index = %llu, count = %i\n", __func__, index, count);
	while (count) {
		listfs_write_block(this, index, buffer);
		index++;
		buffer += this->header->block_size;
		count--;
	}
}

/* Bitmap functions */

void listfs_get_blocks(ListFS *this, ListFS_BlockIndex index, size_t count) {
	if (!this) return;
	listfs_log(this, "[%s] index = %llu, count = %u\n", __func__, index, count);
	this->header->used_blocks += count;
	size_t i = index / 8;
	uint8_t j = index % 8;
	if (j) {
		for (j = index % 8; j < 8; j++) {
			this->map[i] |= 1 << j;
			count--;
		}
		i++;
	}
	while (count >= 8) {
		this->map[i] = 0xFF;
		i++;
		count -= 8;
	}
	for (j = 0; j < count; j++) {
		this->map[i] |= 1 << j;
	}
}

void listfs_free_blocks(ListFS *this, ListFS_BlockIndex index, size_t count) {
	if (!this) return;
	listfs_log(this, "[%s] index = %llu, count = %u\n", __func__, index, count);
	this->header->used_blocks -= count;
	size_t i = index / 8;
	uint8_t j = index % 8;
	if (j) {
		for (j = index % 8; j < 8; j++) {
			this->map[i] &= ~(1 << j);
			count--;
			if (count == 0) break;
		}
		i++;
	}
	while (count >= 8) {
		this->map[i] = 0;
		i++;
		count -= 8;
	}
	for (j = 0; j < count; j++) {
		this->map[i] &= ~(1 << j);
	}
}

ListFS_BlockIndex listfs_alloc_block(ListFS *this) {
	if (!this) return;
	listfs_log(this, "[%s]\n", __func__);
	size_t start_byte = this->last_allocated_block / 8;
	size_t end_byte = bytes_to_blocks(this->header->size, 8);
	size_t byte = start_byte;
	while (this->map[byte] == 0xFF) {
		byte++;
		if (byte == end_byte) {
			byte = 0;
		}
		if (byte == start_byte) {
			listfs_log(this, "[%s] Free block not found\n", __func__);
			return -1;
		}
	}
	uint8_t bit;
	for (bit = 0; bit < 8; bit++) {
		if ((this->map[byte] & (1 << bit)) == 0) {
			break;
		}
	}
	this->map[byte] |= 1 << bit;
	this->last_allocated_block = byte * 8 + bit;
	listfs_log(this, "[%s] Found free block %llu\n", __func__, this->last_allocated_block);
	this->header->used_blocks++;
	return this->last_allocated_block;
}

/* Node functions */

ListFS_NodeHeader *listfs_fetch_node(ListFS *this, ListFS_BlockIndex node) {
	if (!this) return NULL;
	if (node == -1) return NULL;
	listfs_log(this, "[%s] node = %llu\n", __func__, node);
	ListFS_NodeHeader *header = malloc(this->header->block_size);
	listfs_read_block(this, node, header);
	return header;
}

void listfs_insert_node(ListFS *this, ListFS_BlockIndex node, ListFS_BlockIndex parent) {
	if (!this) return;
	if (node == -1) return;
	listfs_log(this, "[%s] node = %llu, parent = %llu\n", __func__, node, parent);
	ListFS_NodeHeader *header = listfs_fetch_node(this, node);
	header->parent = parent;
	header->prev = -1;
	ListFS_NodeHeader *tmp_header = calloc(this->header->block_size, 1);
	if (parent == -1) {
		header->next = this->header->root_dir;
		this->header->root_dir = node;
	} else {
		listfs_read_block(this, parent, tmp_header);
		header->next = tmp_header->data;
		tmp_header->data = node;
		listfs_write_block(this, parent, tmp_header);
	}
	if (header->next != -1) {
		listfs_read_block(this, header->next, tmp_header);
		tmp_header->prev = node;
		listfs_write_block(this, header->next, tmp_header);
	}
	listfs_write_block(this, node, header);
	free(tmp_header);
	free(header);
}

void listfs_remove_node(ListFS *this, ListFS_BlockIndex node) {
	if (!this) return;
	if (node == -1) return;
	listfs_log(this, "[%s] node = %llu\n", __func__, node);
	ListFS_NodeHeader *header = listfs_fetch_node(this, node);
	ListFS_BlockIndex next = header->next, prev = header->prev, parent = header->parent;
	if (next != -1) {
		listfs_read_block(this, next, header);
		header->prev = prev;
		listfs_write_block(this, next, header);
	}
	if (prev != -1) {
		listfs_read_block(this, prev, header);
		header->next = next;
		listfs_write_block(this, prev, header);
	} else {
		if (parent != -1) {
			listfs_read_block(this, parent, header);
			header->data = next;
			listfs_write_block(this, parent, header);
		} else {
			this->header->root_dir = next;
		}
	}
	free(header);
}

ListFS_BlockIndex listfs_create_node(ListFS *this, uint8_t *name, uint32_t flags, ListFS_BlockIndex parent) {
	if (!this) return;
	listfs_log(this, "[%s] name = '%s', flags = %llu, parent = %llu\n", __func__, name, flags, parent);
	ListFS_BlockIndex header_block = listfs_alloc_block(this);
	if (header_block == -1) return -1;
	ListFS_NodeHeader *header = calloc(this->header->block_size, 1);
	header->magic = LISTFS_NODE_MAGIC;
	strncpy(header->name, name, sizeof(header->name));
	header->flags = flags;
	header->data = -1;
#ifndef DISABLE_TIME
	header->create_time = time(NULL);
	header->modify_time = header->create_time;
	header->access_time = header->create_time;
#endif
	listfs_write_block(this, header_block, header);
	listfs_insert_node(this, header_block, parent);
	return header_block;
}

bool listfs_delete_node(ListFS *this, ListFS_BlockIndex node) {
	if (!this) return false;
	if (node == -1) return false;
	listfs_log(this, "[%s] node = %llu\n", __func__, node);
	ListFS_NodeHeader *header = listfs_fetch_node(this, node);
	if (header->data != -1) {
		listfs_log(this, "[%s] Node has data!\n", __func__);
		free(header);
		return false;
	}
	listfs_remove_node(this, node);
	listfs_free_blocks(this, node, 1);
	free(header);
	return true;
}

void listfs_move_node(ListFS *this, ListFS_BlockIndex node, ListFS_BlockIndex new_parent) {
	if (!this) return;
	if (node == -1) return;
	listfs_log(this, "[%s] node = %llu, new_parent = %llu\n", __func__, node, new_parent);
	listfs_remove_node(this, node);
	listfs_insert_node(this, node, new_parent);
}

void listfs_foreach_node(ListFS *this, ListFS_BlockIndex node, bool (*callback)(ListFS*, ListFS_BlockIndex, ListFS_NodeHeader*, void*), void *data) {
	if (!this) return;
	listfs_log(this, "[%s] first node = %llu\n", __func__, node);
	ListFS_NodeHeader *header = calloc(this->header->block_size, 1);
	while (node != -1) {
		listfs_read_block(this, node, header);
		if (callback) {
			if (!callback(this, node, header, data)) break;
		}
		node = header->next;
		listfs_log(this, "[%s] next node = %llu\n", __func__, node);
	}
	free(header);
}

void listfs_foreach_subnode(ListFS *this, ListFS_BlockIndex node, bool (*callback)(ListFS*, ListFS_BlockIndex, ListFS_NodeHeader*, void*), void *data) {
	if (!this) return;
	listfs_log(this, "[%s] parent node = %llu\n", __func__, node);
	if (node != -1) {
		ListFS_NodeHeader *header = listfs_fetch_node(this, node);
		listfs_read_block(this, node, header);
		listfs_foreach_node(this, header->data, callback, data);
		free(header);
	}
}

typedef struct {
	ListFS_BlockIndex node;
	uint64_t flags;
	ListFS_BlockIndex data;
	uint8_t *name;
} ListFS_SearchState;

bool listfs_search_node_callback(ListFS *fs, ListFS_BlockIndex node, ListFS_NodeHeader *header, void *data) {
	ListFS_SearchState *state = data;
	if (strncmp(header->name, state->name, sizeof(header->name)) == 0) {
		state->node = node;
		state->flags = header->flags;
		state->data = header->data;
		return false;
	} else {
		return true;
	}
}

uint64_t listfs_search_node(ListFS *this, uint8_t *path, ListFS_BlockIndex first) {
	listfs_log(this, "[%s] path = '%s', first = %llu\n", __func__, path, first);
	if (!this) return;
	uint8_t node_name[256 + 1];
	char *subpath = strchr(path, '/');
	size_t node_name_len = subpath ? ((size_t)subpath - (size_t)path) : strlen(path);
	if (subpath) subpath++;
	strncpy(node_name, path, min(node_name_len, 256));
	node_name[node_name_len] = 0;
	listfs_log(this, "[%s] node_name = '%s', subpath = '%s'\n", __func__, node_name, subpath);
	ListFS_SearchState state;
	state.node = -1;
	state.name = node_name;
	listfs_foreach_node(this, first, listfs_search_node_callback, &state);
	if (state.node == -1) {
		listfs_log(this, "[%s] Node '%s' not found %llu\n", __func__, node_name);
		return -1;
	} else {
		listfs_log(this, "[%s] Found node %llu\n", __func__, state.node);
		if ((subpath == NULL) || (subpath[0] == 0)) {
			return state.node;
		} else if (state.flags & LISTFS_NODE_FLAG_DIRECTORY) {
			listfs_log(this, "[%s] We going deeper\n", __func__);
			return listfs_search_node(this, subpath, state.data);
		} else {
			listfs_log(this, "[%s] We need directory, but found file\n", __func__);
			return -1;
		}
	}
}

void listfs_rename_node(ListFS *this, ListFS_BlockIndex node, uint8_t *name) {
	listfs_log(this, "[%s] node = %llu, name = '%s'\n", __func__, node, name);
	ListFS_NodeHeader *header = listfs_fetch_node(this, node);
	strncpy(header->name, name, 256);
	listfs_write_block(this, node, header);
	free(header);
}

/* File functions */

ListFS_OpennedFile *listfs_open_file(ListFS *this, ListFS_BlockIndex node) {
	if (!this) return;
	listfs_log(this, "[%s] node = %llu\n", __func__, node);
	if (node == -1) return NULL;
	ListFS_OpennedFile *file;
	size_t i;
	for (i = 0; i < file_info_count; i++) {
		if (file_info[i].node == node) {
			file_info[i].file->link_count++;
			listfs_log(this, "[%s] This file already openned\n", __func__);
			return file_info[i].file;
		}
	}
	file = calloc(sizeof(ListFS_OpennedFile), 1);
	file->fs = this;
	file->node = node;
	file->node_header = calloc(this->header->block_size, 1);
	listfs_read_block(this, node, file->node_header);
	if ((file->node_header->magic != LISTFS_NODE_MAGIC) || (file->node_header->flags & LISTFS_NODE_FLAG_DIRECTORY)) {
		listfs_file_close(file);
		return NULL;
	}
	file->cur_block_list_block = file->node_header->data;
	file->cur_block_list = calloc(this->header->block_size, 1);
	if (file->node_header->data != -1) {
		listfs_read_block(this, file->node_header->data, file->cur_block_list);
	}
	file->cur_block = 1;
	file->link_count++;
	file_info_count++;
	file_info = realloc(file_info, file_info_count * sizeof(FileInfo));
	file_info[file_info_count - 1].node = node;
	file_info[file_info_count - 1].file = file;
	return file;
}

void listfs_file_close(ListFS_OpennedFile *this) {
	if (!this) return;
	listfs_log(this->fs, "[%s] link count = %u\n", __func__, this->link_count);
	this->link_count--;
	if (this->link_count == 0) {
		size_t i;
		for (i = 0; i < file_info_count; i++) {
			if (file_info[i].node = this->node) {
				memmove(&file_info[i], &file_info[i + 1], file_info_count - i - 1);
				file_info_count--;
				file_info = realloc(file_info, file_info_count * sizeof(FileInfo));
				break;
			}
		}
		free(this->node_header);
		free(this->cur_block_list);
		free(this);
	}
}

bool listfs_file_touch_cur_block(ListFS_OpennedFile *this, bool write) {
	if (!this) return false;
	listfs_log(this->fs, "[%s] write = %u\n", __func__, write);
	size_t block_list_size = this->fs->header->block_size / sizeof(ListFS_BlockIndex);
	bool result = false;
	if (this->cur_block_list_block == -1) {
		if (write) {
			this->cur_block_list_block = listfs_alloc_block(this->fs);
			if (this->cur_block_list_block != -1) {
				this->node_header->data = this->cur_block_list_block;
				listfs_write_block(this->fs, this->node, this->node_header);
				memset(this->cur_block_list, -1, block_list_size * sizeof(ListFS_BlockIndex));
				listfs_write_block(this->fs, this->cur_block_list_block, this->cur_block_list);
			}
		}
	}
	if (this->cur_block_list_block != -1) {
		if (this->cur_block == 0) {
			if (this->cur_block_list[0] == -1) {
				this->cur_block = 1;
			} else {
				this->cur_block_list_block = this->cur_block_list[0];
				listfs_read_block(this->fs, this->cur_block_list_block, this->cur_block_list);
				this->cur_block = block_list_size - 2;
			}
		} else if (this->cur_block == block_list_size - 1) {
			if (this->cur_block_list[block_list_size - 1] == -1) {
				if (write) {
					this->cur_block_list[block_list_size - 1] = listfs_alloc_block(this->fs);
					if (this->cur_block_list[block_list_size - 1] != -1) {
						listfs_write_block(this->fs, this->cur_block_list_block, this->cur_block_list);
						ListFS_BlockIndex prev_block_list = this->cur_block_list_block;
						this->cur_block_list_block = this->cur_block_list[block_list_size - 1];
						memset(this->cur_block_list + 1, -1, (block_list_size - 1) * sizeof(ListFS_BlockIndex));
						this->cur_block_list[0] = prev_block_list;
						listfs_write_block(this->fs, this->cur_block_list_block, this->cur_block_list);
						this->cur_block = 1;
					}
				}
			} else {
				this->cur_block_list_block = this->cur_block_list[block_list_size - 1];
				listfs_read_block(this->fs, this->cur_block_list_block, this->cur_block_list);
				this->cur_block = 1;
			}
		}
		if ((this->cur_block > 0) && (this->cur_block < block_list_size - 1)) {
			if (this->cur_block_list[this->cur_block] == -1) {
				if (write) {
					this->cur_block_list[this->cur_block] = listfs_alloc_block(this->fs);
					if (this->cur_block_list[this->cur_block] != -1) {
						listfs_write_block(this->fs, this->cur_block_list_block, this->cur_block_list);
						result = true;
					}
				}
			} else {
				result = true;
			}
		}
	}
	return result;
}

bool listfs_file_switch_cur_block(ListFS_OpennedFile *this, bool prev, bool write) {
	if (!this) return false;
	listfs_log(this->fs, "[%s] prev = %u, write = %u\n", __func__, prev, write);
	size_t block_list_size = this->fs->header->block_size / sizeof(ListFS_BlockIndex);
	bool result;
	if (prev) {
		if (this->cur_block > 0) {
			this->cur_block--;
			if (this->cur_global_offset >= this->fs->header->block_size) {
 				this->cur_global_offset -= this->fs->header->block_size;
			}
		}
	} else {
		if (this->cur_block < block_list_size) {
 			this->cur_block++;
		}
	}
	result = listfs_file_touch_cur_block(this, write);
	if (result && !prev) {
		this->cur_global_offset += this->fs->header->block_size;
	}
	return result;
}

void listfs_file_seek(ListFS_OpennedFile *this, uint64_t offset, bool write) {
	if (!this) return;
	listfs_log(this->fs, "[%s] offset = %llu, write = %u\n", __func__, offset, write);
	while (this->cur_global_offset / this->fs->header->block_size > offset / this->fs->header->block_size) {
		if (!listfs_file_switch_cur_block(this, true, write)) break;
	}
	while (this->cur_global_offset / this->fs->header->block_size < offset / this->fs->header->block_size) {
		if (!listfs_file_switch_cur_block(this, false, write)) break;
	}
 	this->cur_offset = offset % this->fs->header->block_size;
	this->cur_global_offset = offset;
	if ((this->cur_global_offset > this->node_header->size) && write) {
		this->node_header->size = this->cur_global_offset;
		listfs_write_block(this->fs, this->node, this->node_header);
	}
}

void listfs_file_truncate(ListFS_OpennedFile *this) {
	if (!this) return;
	listfs_log(this->fs, "[%s]\n", __func__);
	ListFS_BlockIndex cur_list = this->cur_block_list_block;
	if (cur_list == -1) return;
	size_t cur_block = this->cur_block;
	if (this->cur_offset > 0) {
		cur_block++;
	}
	size_t block_list_size = this->fs->header->block_size / sizeof(ListFS_BlockIndex);
	ListFS_BlockIndex *list = malloc(block_list_size * sizeof(ListFS_BlockIndex));
	listfs_read_block(this->fs, cur_list, list);
	size_t free_blocks = 0;
	while (true) {
		if (cur_block == block_list_size - 1) {
			if (free_blocks == block_list_size - 2) {
				listfs_free_blocks(this->fs, cur_list, 1);
				if (list[0] == -1) {
					this->node_header->data = -1;
					this->cur_block_list_block = -1;
					listfs_write_block(this->fs, this->node, this->node_header);
				} else {
					ListFS_BlockIndex *prev_list = malloc(block_list_size * sizeof(ListFS_BlockIndex));
					listfs_read_block(this->fs, list[0], prev_list);
					list[block_list_size - 1] = -1;
					listfs_write_block(this->fs, list[0], prev_list);
					free(prev_list);
				}
			} else {
				listfs_write_block(this->fs, cur_list, list);
			}
			cur_list = list[cur_block];
			if (cur_list == -1) {
				break;
			}
			listfs_read_block(this->fs, cur_list, list);
			cur_block = 1;
			free_blocks = 0;
		} else {
			if (list[cur_block] != -1) {
				listfs_free_blocks(this->fs, list[cur_block], 1);
				list[cur_block] = -1;
			}
			free_blocks++;
			cur_block++;
		}
	}
	free(list);
	this->node_header->size = this->cur_global_offset;
#ifndef DISABLE_TIME
	this->node_header->modify_time = time(NULL);
#endif
	listfs_write_block(this->fs, this->node, this->node_header);
	if (this->cur_block_list_block != -1) {
		listfs_read_block(this->fs, this->cur_block_list_block, this->cur_block_list);
	}
}

size_t listfs_file_write(ListFS_OpennedFile *this, void *buffer, size_t length) {
	if (!this) return 0;
	listfs_log(this->fs, "[%s] length = %u\n", __func__, length);
	size_t count = 0;
	uint8_t *tmp = calloc(this->fs->header->block_size, 1);
	while (length) {
		if (!listfs_file_touch_cur_block(this, true)) break;
		if ((this->cur_offset > 0) || (length < this->fs->header->block_size)) {
			listfs_read_block(this->fs, this->cur_block_list[this->cur_block], tmp);
		}
		size_t c = min(this->fs->header->block_size - this->cur_offset, length);
		listfs_log(this->fs, "[%s] We writing %u bytes of data at offset %u now\n", __func__, c, this->cur_offset);
		memmove(tmp + this->cur_offset, buffer, c);
		listfs_write_block(this->fs, this->cur_block_list[this->cur_block], tmp);
		buffer += c;
		length -= c;
		count += c;
		this->cur_offset += c;
		this->cur_global_offset += c;
		if (this->cur_offset >= this->fs->header->block_size) {
			this->cur_block++;
			this->cur_offset = 0;
		}
	}
	free (tmp);
	if (this->cur_global_offset > this->node_header->size) {
		this->node_header->size = this->cur_global_offset;
#ifndef DISABLE_TIME
		this->node_header->modify_time = time(NULL);
#endif
		listfs_write_block(this->fs, this->node, this->node_header);
	}
	return count;
}

size_t listfs_file_read(ListFS_OpennedFile *this, void *buffer, size_t length) {
	if (!this) return 0;
	listfs_log(this->fs, "[%s] length = %u\n", __func__, length);
	size_t count = 0;
	uint8_t *tmp = calloc(this->fs->header->block_size, 1);
	length = min(length, this->node_header->size - this->cur_global_offset);
	while (length) {
		if (!listfs_file_touch_cur_block(this, false)) break;
		listfs_read_block(this->fs, this->cur_block_list[this->cur_block], tmp);
		size_t c = min(this->fs->header->block_size - this->cur_offset, length);
		listfs_log(this->fs, "[%s] We reading %u bytes of data at offset %u now\n", __func__, c, this->cur_offset);
		memmove(buffer, tmp + this->cur_offset, c);
		buffer += c;
		length -= c;
		count += c;
		this->cur_offset += c;
		this->cur_global_offset += c;
		if (this->cur_offset >= this->fs->header->block_size) {
			this->cur_block++;
			this->cur_offset = 0;
		}
	}
	free (tmp);
	return count;
}

/* Main functions */

ListFS *listfs_init(void (*read_block_func)(ListFS*, ListFS_BlockIndex, void*),
		void (*write_block_func)(ListFS*, ListFS_BlockIndex, void*), void (*log_func)(ListFS*, char*, va_list)) {
	ListFS *this = calloc(sizeof(ListFS), 1);
	this->read_block_func = read_block_func;
	this->write_block_func = write_block_func;
	this->log_func = log_func;
	return this;
}

void listfs_create(ListFS *this, ListFS_BlockCount size, uint16_t block_size, void *bootloader, size_t bootloader_size) {
	if (!this) return;
	listfs_log(this, "[%s] size = %llu, block_size = %u, bootloader_size = %u\n", __func__, size, block_size, bootloader_size);
	this->header = calloc(max(bootloader_size, block_size), 1);
	if (bootloader) {
		memmove(this->header, bootloader, bootloader_size);
	}
	this->header->magic = LISTFS_MAGIC;
	this->header->version = (LISTFS_VERSION_MAJOR << 8) | LISTFS_VERSION_MINOR;
	this->header->base = 0;
	this->header->size = size;
	this->header->map_base = bytes_to_blocks(bootloader_size ? bootloader_size : sizeof(ListFS_Header), block_size);
	this->header->map_size = bytes_to_blocks(bytes_to_blocks(size, 8), block_size);
	this->header->block_size = block_size;
	this->header->used_blocks = 0;
	this->map = calloc(block_size, this->header->map_size);
	listfs_get_blocks(this, 0, this->header->map_base + this->header->map_size);
	this->header->root_dir = -1;
	listfs_write_blocks(this, 0, this->header, this->header->map_base);
	uint8_t tmp[block_size];
	listfs_write_block(this, size - 1, &tmp);
}

bool listfs_open(ListFS *this) {
	if (!this) return;
	listfs_log(this, "[%s]\n", __func__);
	this->header = malloc(sizeof(ListFS_Header));
	this->header->block_size = sizeof(ListFS_Header);
	this->header->base = 0;
	listfs_read_block(this, 0, this->header);
	if (this->header->magic != LISTFS_MAGIC) {
		listfs_log(this, "[%s] This is not ListFS!\n", __func__);
		return false;
	}
	this->header = realloc(this->header, this->header->block_size);
	listfs_read_block(this, 0, this->header);
	this->map = calloc(this->header->block_size, this->header->map_size);
	listfs_read_blocks(this, this->header->map_base, this->map, this->header->map_size);
	return true;
}

void listfs_close(ListFS *this) {
	if (!this) return;
	listfs_log(this, "[%s]\n", __func__);
	listfs_write_block(this, 0, this->header);
	listfs_write_blocks(this, this->header->map_base, this->map, this->header->map_size);
	free(this->map);
	free(this);
}