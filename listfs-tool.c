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
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "liblistfs.h"

FILE *log_file;
FILE *device_file;
ListFS *fs;

static int _getattr(const char *path, struct stat *stbuf) {
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}
	uint64_t node = listfs_search_node(fs, (char*)path + 1, fs->header.root_dir);
	if (node == -1) {
		return -ENOENT;
	}
	ListFS_NodeHeader *header = listfs_fetch_node(fs, node);
	stbuf->st_nlink = 1;
	if (header->flags & LISTFS_NODE_FLAG_DIRECTORY) {
		stbuf->st_mode = S_IFDIR | 0755;
	} else {
		stbuf->st_mode = S_IFREG | 0755;
	}
	stbuf->st_ctime = header->create_time;
	stbuf->st_mtime = header->modify_time;
	stbuf->st_atime = header->access_time;
	stbuf->st_size = header->size;
	free(header);
	return 0;
}

typedef struct {
	fuse_fill_dir_t filler;
	void *buf;
} ReadDirState;

bool readdir_callback(ListFS *fs, uint64_t node, ListFS_NodeHeader *header, void *data) {
	ReadDirState *state = data;
	state->filler(state->buf, header->name, NULL, 0);
	return true;
}

static int _readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	uint64_t node;
	if (strcmp(path, "/") == 0) {
		node = fs->header.root_dir;
	} else {
		node = listfs_search_node(fs, (char*)path + 1, fs->header.root_dir);
		if (node == -1) {
			return -ENOENT;
		}
		ListFS_NodeHeader *header = listfs_fetch_node(fs, node);
		node = (header->flags & LISTFS_NODE_FLAG_DIRECTORY) ? header->data : -1;
		free(header);
		if (node == -1) {
			return -ENOENT;
		}
	}
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	ReadDirState state;
	state.filler = filler;
	state.buf = buf;
	listfs_foreach_node(fs, node, readdir_callback, &state);
	return 0;
}

int _make_node(const char *path, uint32_t flags) {
	char *_path = strdup(path);
	char *parent_name = strdup(dirname(_path));
	free(_path);
	const char *file_name = path + strlen(parent_name);
	if (file_name[0] == '/') file_name++;
	uint64_t parent;
	if (strcmp(parent_name, "/") == 0) {
		parent = -1;
	} else {
		parent = listfs_search_node(fs, parent_name + 1, fs->header.root_dir);
		if (parent == -1) {
			free(parent_name);
			return -ENOENT;
		}
	}
	free(parent_name);
	return (listfs_create_node(fs, (char*)file_name, flags, parent) ? 0 : -EACCES);
}

static int _mknod(const char *path, mode_t mode, dev_t rdev) {
	return _make_node(path, 0);
}

static int _mkdir(const char *path, mode_t mode) {
	return _make_node(path, LISTFS_NODE_FLAG_DIRECTORY);
}

static int _unlink(const char *path) {
	uint64_t node = listfs_search_node(fs, (char*)path + 1, fs->header.root_dir);
	if (node == -1) return -ENOENT;
	ListFS_OpennedFile *file = listfs_open_file(fs, node);
	if (file) {
		listfs_file_seek(file, 0, false);
		listfs_file_truncate(file);
		listfs_file_close(file);
	}
	if (listfs_delete_node(fs, node)) {
		return 0;
	} else {
		return -EACCES;
	}
}

static int _rmdir(const char *path) {
	uint64_t node = listfs_search_node(fs, (char*)path + 1, fs->header.root_dir);
	if (node == -1) return -ENOENT;
	if (listfs_delete_node(fs, node)) {
		return 0;
	} else {
		return -EACCES;
	}
}

static int _rename(const char *from, const char *to) {
	uint64_t node = listfs_search_node(fs, (char*)from + 1, fs->header.root_dir);
	if (node == -1) return -ENOENT;
	char *_path = strdup(to);
	char *parent_name = strdup(dirname(_path));
	free(_path);
	const char *file_name = to + strlen(parent_name);
	if (file_name[0] == '/') file_name++;
	uint64_t parent;
	if (strcmp(parent_name, "/") == 0) {
		parent = -1;
	} else {
		parent = listfs_search_node(fs, parent_name + 1, fs->header.root_dir);
		if (parent == -1) {
			free(parent_name);
			return -ENOENT;
		}
	}
	free(parent_name);
	listfs_move_node(fs, node, parent);
	listfs_rename_node(fs, node, (char*)file_name);
	return 0;
}

static int _open(const char *path, struct fuse_file_info *fi) {
	ListFS_OpennedFile *file = listfs_open_file(fs, listfs_search_node(fs, (char*)path + 1, fs->header.root_dir));
	if (!file) {
		return -ENOENT;
	}
	fi->fh = (size_t)file;
	return 0;
}

static int _release(const char *path, struct fuse_file_info *fi) {
	listfs_file_close((void*)fi->fh);
}

static int _read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	ListFS_OpennedFile *file = (void*)fi->fh;
	listfs_file_seek(file, offset, false);
	listfs_file_read(file, buf, size);
}

static int _write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	ListFS_OpennedFile *file = (void*)fi->fh;
	listfs_file_seek(file, offset, true);
	listfs_file_write(file, (char*)buf, size);
}

static int _truncate(const char *path, off_t size) {
	ListFS_OpennedFile *file = listfs_open_file(fs, listfs_search_node(fs, (char*)path + 1, fs->header.root_dir));
	if (!file) {
		return -ENOENT;
	}
	listfs_file_seek(file, size, true);
	listfs_file_truncate(file);
	listfs_file_close(file);
	return 0;
}

void _destroy() {
	listfs_close(fs);
}

static struct fuse_operations listfs_operations = {
	.getattr = _getattr,
	.readdir = _readdir,
	.mknod = _mknod,
	.mkdir = _mkdir,
	.unlink = _unlink,
	.rmdir = _rmdir,
	.rename = _rename,
	.open = _open,
	.release = _release,
	.read = _read,
	.write = _write,
	.truncate = _truncate,
	.destroy = _destroy
};

void display_usage() {
	printf("ListFS Tool. Version %i.%i\n", LISTFS_VERSION_MAJOR, LISTFS_VERSION_MINOR);
	printf("Usage:\n");
	printf("\tlistfs-tool create <file or device name> <file system size in blocks> <block size>\n");
	printf("\tlistfs-tool dump <file or device name>\n");
	printf("\tlistfs-tool mount <file or device name> <mount point> [fuse options]\n");
	printf("\n");
}

void read_block_func(ListFS *fs, uint64_t index, void *buffer) {
	fseek(device_file, index * fs->header.block_size + fs->header.base, SEEK_SET);
	fread(buffer, fs->header.block_size, 1, device_file);
}

void write_block_func(ListFS *fs, uint64_t index, void *buffer) {
	fseek(device_file, index * fs->header.block_size + fs->header.base, SEEK_SET);
	fwrite(buffer, fs->header.block_size, 1, device_file);
}

void log_func(ListFS *fs, char *fmt, va_list ap) {
	vfprintf(log_file, fmt, ap);
}

char readme_text[] = "This is first file on your ListFS!\n";

int main(int argc, char *argv[]) {
	if (argc < 3) {
		display_usage();
		return 0;
	}
	log_file = fopen("/tmp/listfs-tool.log", "w");
	fs = listfs_init(read_block_func, write_block_func, log_func);
	char *action = argv[1];
	char *file_name = argv[2];
	if (strcmp(action, "create") == 0) {
		if (argc < 5) {
			display_usage();
			return 0;
		}
		uint64_t fs_size = atol(argv[3]);
		uint32_t fs_block_size = atoi(argv[4]);
		if (fs_size < 2) {
			printf("FS size too small!\n");
			return -1;
		}
		if (fs_block_size < LISTFS_MIN_BLOCK_SIZE) {
			printf("Block size must be greater than %u bytes!\n", LISTFS_MIN_BLOCK_SIZE);
			return -1;
		}
		device_file = fopen(file_name, "w+");
		listfs_create(fs, atol(argv[3]), atoi(argv[4]));
		ListFS_OpennedFile *file = listfs_open_file(fs, listfs_create_node(fs, "README", 0, -1));
		listfs_file_write(file, readme_text, strlen(readme_text));
		listfs_file_close(file);
		listfs_close(fs);
		return 0;
	} else if (strcmp(action, "mount") == 0) {
		if (argc < 4) {
			display_usage();
			return 0;
		}
		device_file = fopen(file_name, "r+");
		if (!listfs_open(fs)) {
			fprintf(stderr, "Failed to open ListFS volume! Maybe this is not ListFS?\n");
		}
		int i;
		for (i = 3; i < argc; i++) {
			argv[i - 2] = argv[i];
		}
		return fuse_main(argc - 2, argv, &listfs_operations, NULL);
	} else {
		printf("Unknown action: %s!\n", action);
		return -10;
	}
}