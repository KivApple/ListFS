#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "listfs.h"

/* common */

uint64_t bytes_to_blocks(uint64_t bytes, uint64_t block_size) {
	return (bytes + block_size - 1) / block_size;
}

/* mkfs */

int listfs_create(char *file_name, uint64_t size, char *boot_file_name) {
	listfs_header *fs_header = calloc(1, LISTFS_BLOCK_SIZE);
	if (boot_file_name) {
		FILE *boot_file = fopen(boot_file_name, "r");
		if (!boot_file) {
			return -2;
		}
		fread(fs_header, LISTFS_BLOCK_SIZE, 1, boot_file);
		fclose(boot_file);
	}
	FILE *fs_device = fopen(file_name, "w");
	if (!fs_device) {
		printf("Failed to open %s!\n", file_name);
		return -1;
	}
	fs_header->magic = LISTFS_MAGIC;
	fs_header->version = LISTFS_VERSION;
	fs_header->attrs = 0;
	fs_header->block_size = LISTFS_BLOCK_SIZE;
	fs_header->base = 0;
	fs_header->size = size;
	fs_header->map_base = 1;
	fs_header->map_size = bytes_to_blocks(bytes_to_blocks(size, 8), LISTFS_BLOCK_SIZE);
	fs_header->first_file = -1;
	fs_header->uid = (time(NULL) << 16) | (rand() & 0xFFFF);
	fwrite(fs_header, LISTFS_BLOCK_SIZE, 1, fs_device);
	uint8_t *fs_map = calloc(LISTFS_BLOCK_SIZE, fs_header->map_size);
	{
		uint64_t reserved_blocks = fs_header->map_size + 1; // bitmap + fs header
		memset(fs_map, 0xFF, reserved_blocks / 8);
		uint8_t j = reserved_blocks % 8;
		uint8_t i;
		for (i = 0; i < j; i++) {
			fs_map[reserved_blocks / 8] |= 1 << i;	
		}
	}
	fwrite(fs_map, LISTFS_BLOCK_SIZE, fs_header->map_size, fs_device);
	fseek(fs_device, LISTFS_BLOCK_SIZE * fs_header->size - 1, SEEK_SET);
	{
		uint8_t tmp = 0;
		fwrite(&tmp, 1, 1, fs_device);
	}
	fclose(fs_device);
	return 0;
}

/* fuse */

FILE *fs_device;
listfs_header fs_header;
uint8_t *fs_map;

void listfs_read_block(uint64_t block_index, void *buffer) {
	fseek(fs_device, (block_index + fs_header.base) * fs_header.block_size, SEEK_SET);
	fread(buffer, fs_header.block_size, 1, fs_device);
}

void listfs_write_block(uint64_t block_index, void *buffer) {
	fseek(fs_device, (block_index + fs_header.base) * fs_header.block_size, SEEK_SET);
	fwrite(buffer, fs_header.block_size, 1, fs_device);
}

void listfs_read_blocks(uint64_t block_index, void *buffer, size_t count) {
	size_t i;
	for (i = block_index; i < block_index + count; i++) {
		listfs_read_block(i, buffer);
		buffer += fs_header.block_size;
	}
}

uint64_t listfs_search_file(uint64_t first_file, const char *path, listfs_file_header *file_header) {
	char file_name[LISTFS_MAX_FILE_NAME];
	char *subfile_name = strchr(path, '/');
	if (subfile_name) {
		*subfile_name = '\0';
		strncpy(file_name, path, LISTFS_MAX_FILE_NAME);
		*subfile_name = '/';
		subfile_name++;
	} else {
		strncpy(file_name, path, LISTFS_MAX_FILE_NAME);
	}
	uint64_t cur_file = first_file;
	while (cur_file != -1) {
		listfs_read_block(cur_file, file_header);
		if (strncmp(file_name, file_header->name, LISTFS_MAX_FILE_NAME) == 0) {
			if (subfile_name) {
				if (file_header->attrs & LISTFS_FILE_ATTR_DIR) {
					return listfs_search_file(file_header->data, subfile_name, file_header);
				}
			} else {
				return cur_file;
			}
		}
		cur_file = file_header->next;
	}
	return -1;
}

static int listfs_getattr(const char *path, struct stat *stbuf) {
	int res = 0;
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		char *_path = strdup(path + 1);
		listfs_file_header *file_header = malloc(fs_header.block_size);
		uint64_t file_header_block = listfs_search_file(fs_header.first_file, _path, file_header);
		free(_path);
		if (file_header_block == -1) {
			res = -ENOENT;
		} else {
			if (file_header->attrs & LISTFS_FILE_ATTR_DIR) {
				stbuf->st_mode = S_IFDIR | 0777;
				stbuf->st_nlink = 2;
			} else {
				stbuf->st_mode = S_IFREG | 0777;
				stbuf->st_nlink = 1;
				stbuf->st_size = file_header->size;
				stbuf->st_atime = file_header->access_time;
				stbuf->st_ctime = file_header->create_time;
				stbuf->st_mtime = file_header->modify_time;
			}
		}
		free(file_header);
	}
	return res;
}

static int listfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	listfs_file_header *file_header = malloc(fs_header.block_size);
	uint64_t file_header_block;
	if (strcmp(path, "/") == 0) {
		file_header_block = fs_header.first_file;
	} else {
		char *_path = strdup(path + 1);
		file_header_block = listfs_search_file(fs_header.first_file, _path, file_header);
		free(_path);
		if ((file_header_block == -1) && (file_header->attrs & LISTFS_FILE_ATTR_DIR)) {
			free(file_header);
			return -ENOENT;
		}
		file_header_block = file_header->data;
	}
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	while (file_header_block != -1) {
		listfs_read_block(file_header_block, file_header);
		filler(buf, file_header->name, NULL, 0);
		file_header_block = file_header->next;
	}
	free(file_header);
	return 0;
}

static int listfs_mknod(const char *path, mode_t mode, dev_t rdev) {
	// TODO
	return -EACCES;
}

static int listfs_mkdir(const char *path, mode_t mode) {
	// TODO
	return -EACCES;
}

static int listfs_unlink(const char *path) {
	// TODO
	return -EACCES;
}

static int listfs_rmdir(const char *path) {
	// TODO
	return -EACCES;
}

static int listfs_rename(const char *from, const char *to) {
	// TODO
	return -EACCES;
}

static int listfs_open(const char *path, struct fuse_file_info *fi) {
	listfs_file_header *file_header = malloc(fs_header.block_size);
	char *_path = strdup(path + 1);
	uint64_t file_header_block = listfs_search_file(fs_header.first_file, _path, file_header);
	free(_path);
	if (file_header_block == -1) {
		free(file_header);
		return -ENOENT;
	} else if (((fi->flags & 3) != O_RDONLY) || (file_header->attrs & LISTFS_FILE_ATTR_DIR)) {
		free(file_header);
		// TODO
		return -EACCES;
	}
	fi->fh = (size_t)file_header;
	return 0;
}

static int listfs_release(const char *path, struct fuse_file_info *fi) {
	free((void*)fi->fh);
}

size_t min(size_t a, size_t b) {
	return (a < b) ? a : b;
}

static int listfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	listfs_file_header *file_header = (void*)fi->fh;
	uint64_t block_list_block = file_header->data;
	listfs_block_list *block_list = malloc(fs_header.block_size);
	int count = 0;
	while ((block_list_block != -1) && size) {
		listfs_read_block(block_list_block, block_list);
		size_t i = 0;
		while (size && (block_list->blocks[i] != -1) && (i < (sizeof(block_list->blocks) / sizeof(uint64_t)))) {
			if (offset < fs_header.block_size) {
				uint8_t block[fs_header.block_size];
				listfs_read_block(block_list->blocks[i], block);
				size_t l = min(fs_header.block_size - offset, size);
				memmove(buf + count, block + offset, l);
				count += l;
				size -= l;
				offset = 0;
			} else {
				offset -= fs_header.block_size;
			}
			i++;
		}
		block_list_block = block_list->next_list;
	}
	free(block_list);
	return count;
}

static int listfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	// TODO
	return -EACCES;
}

static int listfs_truncate(const char *path, off_t size) {
	// TODO
	return -EACCES;
}

static struct fuse_operations listfs_operations = {
	.getattr = listfs_getattr,
	.readdir = listfs_readdir,
	.mknod = listfs_mknod,
	.mkdir = listfs_mkdir,
	.unlink = listfs_unlink,
	.rmdir = listfs_rmdir,
	.rename = listfs_rename,
	.open = listfs_open,
	.release = listfs_release,
	.read = listfs_read,
	.write = listfs_write,
	.truncate = listfs_truncate
};

int listfs_mount(char *file_name, int argc, char *argv[]) {
	fs_device = fopen(file_name, "rw");
	if (!fs_device) {
		printf("Failed to open %s!\n", file_name);
		return -1;
	}
	fread(&fs_header, sizeof(fs_header), 1, fs_device);
	if (fs_header.magic != LISTFS_MAGIC) {
		fclose(fs_device);
		printf("This is not ListFS!\n");
		return -3;
	}
	fs_map = malloc(fs_header.map_size * fs_header.block_size);
	listfs_read_blocks(fs_header.map_base, fs_map, fs_header.map_size);
	int i;
	for (i = 3; i < argc; i++) {
		argv[i - 2] = argv[i];
	}
	return fuse_main(argc - 2, argv, &listfs_operations, NULL);
}

/* main */

void display_usage() {
	printf("ListFS Tool. Version %i.%i\n", LISTFS_VERSION_MAJOR, LISTFS_VERSION_MINOR);
	printf("Usage:\n");
	printf("\tlistfs-tool create <file or device name> <file system size in KiB> [boot loader file name]\n");
	printf("\tlistfs-tool mount <file or device name> <mount point> [fuse options]\n");
	printf("\n");
}

int main(int argc, char *argv[]) {
	if (argc < 4) {
		display_usage();
		return 0;
	}
	char *action = argv[1];
	char *file_name = argv[2];
	if (strcmp(action, "create") == 0) {
		return listfs_create(file_name, atol(argv[3]), (argc > 4) ? argv[4] : NULL);
	} else if (strcmp(action, "mount") == 0) {
		return listfs_mount(file_name, argc, argv);
	}
}