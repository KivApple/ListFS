#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include "listfs.h"

/* common */

FILE *log_file;
FILE *fs_device;
listfs_header *fs_header;
uint8_t *fs_map;

void log_message(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(log_file, fmt, args);
	va_end(args);
}

size_t min(size_t a, size_t b) {
	return (a < b) ? a : b;
}

size_t max(size_t a, size_t b) {
	return (a > b) ? a : b;
}

uint64_t bytes_to_blocks(uint64_t bytes, uint64_t block_size) {
	return (bytes + block_size - 1) / block_size;
}

void listfs_read_block(uint64_t block_index, void *buffer) {
	//log_message("[%s] block %i\n", __func__, block_index);
	fseek(fs_device, (block_index + fs_header->base) * fs_header->block_size, SEEK_SET);
	fread(buffer, fs_header->block_size, 1, fs_device);
}

void listfs_write_block(uint64_t block_index, void *buffer) {
	//log_message("[%s] block %i\n", __func__, block_index);
	fseek(fs_device, (block_index + fs_header->base) * fs_header->block_size, SEEK_SET);
	if (fwrite(buffer, fs_header->block_size, 1, fs_device) < fs_header->block_size) {
		//log_message("[%s] write block %i failed!\n", __func__, block_index);
	}
}

void listfs_read_blocks(uint64_t block_index, void *buffer, size_t count) {
	size_t i;
	for (i = block_index; i < block_index + count; i++) {
		listfs_read_block(i, buffer);
		buffer += fs_header->block_size;
	}
}

void listfs_write_blocks(uint64_t block_index, void *buffer, size_t count) {
	size_t i;
	for (i = block_index; i < block_index + count; i++) {
		listfs_write_block(i, buffer);
		buffer += fs_header->block_size;
	}
}

void listfs_store_map() {
	log_message("[%s]\n", __func__);
 	listfs_write_blocks(fs_header->map_base, fs_map, fs_header->map_size);
}

void listfs_get_blocks(uint64_t base, size_t count) {
	uint8_t i = base / 8;
	uint8_t j = base % 8;
	uint8_t k;
	if (j) {
		for (k = j; k < min(8, j + count); k++) {
			fs_map[i] |= 1 << k;
		}
		i++;
		count -= k - j;
	}
	j = count / 8;
	memset(fs_map + i, 0xFF, j);
	i += j;
	count -= j * 8;
	if (count) {
		for (k = 0; k < count; k++) {
			fs_map[i] |= 1 << k;
		}
	}
}

// TODO: Optimize
uint64_t listfs_alloc_blocks(size_t count) {
	log_message("[%s] count=%i\n", __func__, count);
	if (count != 1) return -1; // TODO
	size_t i, j;
	for (i = 0; i < fs_header->map_size * fs_header->block_size; i++) {
		if (fs_map[i] != 0xFF) {
			for (j = 0; j < 8; j++) {
				if ((fs_map[i] & (1 << j)) == 0) {
					fs_map[i] |= 1 << j;
					uint64_t block = i * 8 + j;
					log_message("[%s] Found free block %i!\n", __func__, block);
					return block;
				}
			}
		}
	}
	printf("[%s] No free block found!\n", __func__);
	return -1;
}

void listfs_free_blocks(uint64_t base, size_t count) {
	log_message("[%s] base=%i, count=%i\n", __func__, base, count);
	uint8_t i = base / 8;
	uint8_t j = base % 8;
	uint8_t k;
	if (j) {
		for (k = j; k < min(8, j + count); k++) {
			fs_map[i] &= ~(1 << k);
		}
		i++;
		count -= k - j;
	}
	j = count / 8;
	memset(fs_map + i, 0, j);
	i += j;
	count -= j * 8;
	if (count) {
		for (k = 0; k < count; k++) {
			fs_map[i] &= ~(1 << k);
		}
	}
}

uint64_t listfs_search_file(uint64_t first_file, const char *path, listfs_file_header *file_header) {
	log_message("[%s] first_file=%i, path=%s\n", __func__, first_file, path);
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

/* mkfs */

int listfs_create(char *file_name, uint64_t size, char *boot_file_name) {
	fs_header = calloc(1, LISTFS_BLOCK_SIZE);
	if (boot_file_name) {
		FILE *boot_file = fopen(boot_file_name, "r");
		if (!boot_file) {
			return -2;
		}
		fread(fs_header, LISTFS_BLOCK_SIZE, 1, boot_file);
		fclose(boot_file);
	}
	fs_device = fopen(file_name, "w");
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
	fs_header->map_size = bytes_to_blocks(bytes_to_blocks(size, 8), fs_header->block_size);
	fs_header->first_file = -1;
	fs_header->uid = (time(NULL) << 16) | (rand() & 0xFFFF);
	fwrite(fs_header, fs_header->block_size, 1, fs_device);
	fs_map = calloc(fs_header->block_size, fs_header->map_size);
	/* {
		uint64_t reserved_blocks = fs_header->map_size + 1; // bitmap + fs header
		memset(fs_map, 0xFF, reserved_blocks / 8);
		uint8_t j = reserved_blocks % 8;
		uint8_t i;
		for (i = 0; i < j; i++) {
			fs_map[reserved_blocks / 8] |= 1 << i;	
		}
	} */
	listfs_get_blocks(0, fs_header->map_size + 1); // bitmao + fs header
	fwrite(fs_map, fs_header->block_size, fs_header->map_size, fs_device);
	fseek(fs_device, fs_header->block_size * fs_header->size - 1, SEEK_SET);
	{
		uint8_t tmp = 0;
		fwrite(&tmp, 1, 1, fs_device);
	}
	fclose(fs_device);
	return 0;
}

/* fuse */

static int listfs_getattr(const char *path, struct stat *stbuf) {
	log_message("[%s] path='%s'\n", __func__, path);
	int res = 0;
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		char *_path = strdup(path + 1);
		listfs_file_header *file_header = malloc(fs_header->block_size);
		uint64_t file_header_block = listfs_search_file(fs_header->first_file, _path, file_header);
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
	log_message("[%s] path='%s', offset = %i\n", __func__, path, offset);
	listfs_file_header *file_header = malloc(fs_header->block_size);
	uint64_t file_header_block;
	if (strcmp(path, "/") == 0) {
		file_header_block = fs_header->first_file;
	} else {
		char *_path = strdup(path + 1);
		file_header_block = listfs_search_file(fs_header->first_file, _path, file_header);
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
	log_message("[%s] path='%s'\n", __func__, path);
	// TODO
	return -EACCES;
}

static int listfs_mkdir(const char *path, mode_t mode) {
	log_message("[%s] path='%s'\n", __func__, path);
	// TODO
	return -EACCES;
}

static int listfs_unlink(const char *path) {
	log_message("[%s] path='%s'\n", __func__, path);
	// TODO
	return -EACCES;
}

static int listfs_rmdir(const char *path) {
	log_message("[%s] path='%s'\n", __func__, path);
	// TODO
	return -EACCES;
}

static int listfs_rename(const char *from, const char *to) {
	log_message("[%s] from='%s', to='%s'\n", __func__, from, to);
	// TODO
	return -EACCES;
}

static int listfs_open(const char *path, struct fuse_file_info *fi) {
	log_message("[%s] path='%s'\n", __func__, path);
	listfs_file_header *file_header = malloc(fs_header->block_size);
	char *_path = strdup(path + 1);
	uint64_t file_header_block = listfs_search_file(fs_header->first_file, _path, file_header);
	free(_path);
	if (file_header_block == -1) {
		free(file_header);
		return -ENOENT;
	} else if (file_header->attrs & LISTFS_FILE_ATTR_DIR) {
		free(file_header);
		return -EACCES;
	}
	fi->fh = file_header_block;
	free(file_header);
	return 0;
}

static int listfs_release(const char *path, struct fuse_file_info *fi) {
	log_message("[%s] path='%s'\n", __func__, path);
	//free((void*)fi->fh);
}

static int listfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	log_message("[%s] path='%s', size=%i, offset=%i\n", __func__, path, size, offset);
	uint64_t file_header_block = fi->fh;
	listfs_file_header *file_header = malloc(fs_header->block_size);
	listfs_read_block(file_header_block, file_header);
	uint64_t block_list_block = file_header->data;
	listfs_block_list *block_list = malloc(fs_header->block_size);
	int count = 0;
	while ((block_list_block != -1) && size) {
		listfs_read_block(block_list_block, block_list);
		size_t i = 0;
		while (size && (block_list->blocks[i] != -1) && (i < (sizeof(block_list->blocks) / sizeof(uint64_t)))) {
			if (offset < fs_header->block_size) {
				uint8_t block[fs_header->block_size];
				listfs_read_block(block_list->blocks[i], block);
				size_t l = min(fs_header->block_size - offset, size);
				memmove(buf + count, block + offset, l);
				count += l;
				size -= l;
				offset = 0;
			} else {
				offset -= fs_header->block_size;
			}
			i++;
		}
		block_list_block = block_list->next_list;
	}
	free(block_list);
	return count;
}

static int listfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	log_message("[%s] path='%s', size=%i, offset=%i\n", __func__, path, size, offset);
	uint64_t file_header_block = fi->fh;
	listfs_file_header *file_header = malloc(fs_header->block_size);
	listfs_read_block(file_header_block, file_header);
	uint64_t block_list_block = file_header->data, prev_block_list = -1;
	listfs_block_list *block_list = malloc(fs_header->block_size);
	int count = 0;
	size_t cur_block = 0;
	off_t ofs = offset;
	size_t sz = size;
	log_message("[%s] block_list_block = %i\n", __func__, block_list_block);
	while (sz) {
		if (block_list_block == -1) {
			log_message("[%s] Append new block list\n", __func__);
			uint64_t new_block_list = listfs_alloc_blocks(1);
			if (new_block_list == -1) {
				break;
			}
			if (prev_block_list == -1) {
				file_header->data = new_block_list;
			} else {
				block_list->next_list = new_block_list;
				listfs_write_block(prev_block_list, block_list);
			}
			block_list_block = new_block_list;
			memset(block_list, -1, fs_header->block_size);
		} else {
			listfs_read_block(block_list_block, block_list);
		}
		if (block_list->blocks[cur_block] == -1) {
			block_list->blocks[cur_block] = listfs_alloc_blocks(1);
			if (block_list->blocks[cur_block] == -1) {
				break;
			}
			listfs_write_block(block_list_block, block_list);
		}
		if (ofs >= fs_header->block_size) {
			ofs -= fs_header->block_size;
		} else {
			uint8_t buffer[fs_header->block_size];
			if ((ofs != 0) || (sz < fs_header->block_size)) {
				listfs_read_block(block_list->blocks[cur_block], buffer);
			} else {
				memset(buffer, 0, fs_header->block_size);
			}
			size_t j = min(fs_header->block_size - ofs, sz);
			memmove(buffer + ofs, buf, j);
			listfs_write_block(block_list->blocks[cur_block], buffer);
			ofs = 0;
			sz -= j;
			buf += j;
			count += j;
		}
		cur_block++;
		if (cur_block >= (sizeof(block_list->blocks) / sizeof(uint64_t))) {
			cur_block = 0;
			prev_block_list = block_list_block;
			block_list_block = block_list->next_list;
		}
	}
	free(block_list);
	file_header->size = max(file_header->size, offset + size);
	listfs_store_map();
	listfs_write_block(file_header_block, file_header);
	return count;
}

static int listfs_truncate(const char *path, off_t size) {
	log_message("[%s] path='%s', size=%i\n", __func__, path, size);
	listfs_file_header *file_header = malloc(fs_header->block_size);
	char *_path = strdup(path + 1);
	uint64_t file_header_block = listfs_search_file(fs_header->first_file, _path, file_header);
	free(_path);
	if (file_header_block == -1) {
		free(file_header);
		return -ENOENT;
	} else if (file_header->attrs & LISTFS_FILE_ATTR_DIR) {
		free(file_header);
		return -EACCES;
	}
	if (bytes_to_blocks(size, fs_header->block_size) != bytes_to_blocks(file_header->size, fs_header->block_size)) {
		uint64_t block_list_block = file_header->data, prev_block_list = -1;
		listfs_block_list *block_list = malloc(fs_header->block_size);
		off_t sz = size;
		size_t cur_block = 0;
		while (sz) {
			if (block_list_block == -1) {
				log_message("[%s] Append new block list\n", __func__);
				uint64_t new_block_list = listfs_alloc_blocks(1);
				if (new_block_list == -1) {
					break;
				}
				if (prev_block_list == -1) {
					file_header->data = new_block_list;
				} else {
					block_list->next_list = new_block_list;
					listfs_write_block(prev_block_list, block_list);
				}
				block_list_block = new_block_list;
				memset(block_list, -1, fs_header->block_size);
			} else {
				listfs_read_block(block_list_block, block_list);
			}
			if (block_list->blocks[cur_block] == -1) {
				block_list->blocks[cur_block] = listfs_alloc_blocks(1);
				if (block_list->blocks[cur_block] == -1) {
					break;
				}
				listfs_write_block(block_list_block, block_list);
			}
			if (sz > fs_header->block_size) {
				sz -= fs_header->block_size;
			} else {
				sz = 0;
			}
			cur_block++;
			if (cur_block >= (sizeof(block_list->blocks) / sizeof(uint64_t))) {
				cur_block = 0;
				prev_block_list = block_list_block;
				block_list_block = block_list->next_list;
			}
		}
		size_t free_count = 0;
		while (block_list_block != -1) {
			listfs_read_block(block_list_block, block_list);
			if (block_list->blocks[cur_block] != -1) {
				log_message("[%s] Free block #%i in list %i\n", __func__, cur_block, block_list_block);
				listfs_free_blocks(block_list->blocks[cur_block], 1);
				block_list->blocks[cur_block] = -1;
				listfs_write_block(block_list_block, block_list);
			}
			free_count++;
			cur_block++;
			if (cur_block >= (sizeof(block_list->blocks) / sizeof(uint64_t))) {
				uint64_t next_list = block_list->next_list;
				if (cur_block == free_count) {
					log_message("[%s] Free block list %i\n", __func__, block_list_block);
					listfs_read_block(prev_block_list, block_list);
					block_list->next_list = -1;
					listfs_write_block(prev_block_list, block_list);
					listfs_free_blocks(block_list_block, 1);
				}
				prev_block_list = block_list_block;
				block_list_block = next_list;
				cur_block = 0;
			}
		}
		log_message("[%s] sz=%i, size=%i\n", __func__, sz, size);
		if (size == 0) {
			log_message("[%s] file must be null\n", __func__);
			file_header->data = -1;
		}
		free(block_list);
	}
	file_header->size = size;
	listfs_store_map();
	listfs_write_block(file_header_block, file_header);
	free(file_header);
	return 0;
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
	log_file = fopen("listfs.log", "w");
	log_message("Trying to mount %s on %s...\n", file_name, argv[3]);
	fs_device = fopen(file_name, "r+");
	if (!fs_device) {
		printf("Failed to open %s!\n", file_name);
		return -1;
	}
	log_message("Reading ListFS header...\n");
	fs_header = malloc(sizeof(listfs_header));
	fread(fs_header, sizeof(listfs_header), 1, fs_device);
	if (fs_header->magic != LISTFS_MAGIC) {
		fclose(fs_device);
		printf("This is not ListFS!\n");
		return -3;
	}
	log_message("Detected ListFS!\nVersion: 0x%X\nBase: %i, size: %i\nBitmap base: %i, size: %i\n", fs_header->version, fs_header->base, fs_header->size,
	       	fs_header->map_base, fs_header->map_size);
	log_message("Reading ListFS bitmap...\n");
	fs_map = malloc(fs_header->map_size * fs_header->block_size);
	listfs_read_blocks(fs_header->map_base, fs_map, fs_header->map_size);
	int i;
	for (i = 3; i < argc; i++) {
		argv[i - 2] = argv[i];
	}
	log_message("Starting FUSE...\n");
	fflush(stdout);
	return fuse_main(argc - 2, argv, &listfs_operations, NULL);
}

/* main */

void display_usage() {
	printf("ListFS Tool. Version %i.%i\n", LISTFS_VERSION_MAJOR, LISTFS_VERSION_MINOR);
	printf("Usage:\n");
	printf("\tlistfs-tool create <file or device name> <file system size in blocks> [boot loader file name]\n");
	printf("\tlistfs-tool mount <file or device name> <mount point> [fuse options]\n");
	printf("\n");
}

int main(int argc, char *argv[]) {
	if (argc < 4) {
		display_usage();
		return 0;
	}
	printf("ListFS Tool. Version %i.%i\n", LISTFS_VERSION_MAJOR, LISTFS_VERSION_MINOR);
	char *action = argv[1];
	char *file_name = argv[2];
	if (strcmp(action, "create") == 0) {
		return listfs_create(file_name, atol(argv[3]), (argc > 4) ? argv[4] : NULL);
	} else if (strcmp(action, "mount") == 0) {
		return listfs_mount(file_name, argc, argv);
	} else {
		printf("Unknown action: %s!\n", action);
		return -10;
	}
}