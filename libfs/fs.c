#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

struct SuperBlock{
	uint64_t SIGNATURE;
	uint16_t TOTAL_BLOCKS_COUNTS;
	uint16_t ROOT_DIRECTORY_BLOCK;
	uint16_t DATA_BLOCK;
	uint16_t DATA_BLOCK_COUNT;
	uint8_t FAT_BLOCK_COUNT;
	uint8_t PADDING[4079];
};

// struct Fat{
// 	// uint16_t ENTRIES[FS_FILENAME_LEN];
// 	// uint8_t FAT_INDEX; // datablock start index
// 	uint16_t *FAT;
// };

struct file {
	uint8_t FILENAME[FS_FILENAME_LEN];
	uint32_t FILE_SIZE;
	uint16_t FILE_FIRST_BLOCK;
	uint8_t FILE_PADDING[10];
};

struct RootDirectory{
	struct file all_files[FS_FILE_MAX_COUNT];
};

struct file_desc{
	struct file* cur_file;
	int offset;
};

uint16_t *FAT;
struct SuperBlock super_block;
struct RootDirectory root_directory;
struct file_desc *fd_table[FS_OPEN_MAX_COUNT];
int disk_opened;
int current_open_amount;

int memFree(void){
	free(FAT);
	free((void*)&super_block);
	free((void*)&root_directory);
	return 0;
}


//=====
int fs_mount(const char *diskname)
{
	int disk_opened = block_disk_open(diskname);
	if(disk_opened == -1){
		perror("Fail to open disk");
		return -1;
	}
	block_read(0, (void*)&super_block);
	// block_read(sizeof(super_block), (void*)&super_block);
	if(strcmp(super_block.SIGNATURE, "ECS150FS") != 0){
		perror("Signature False");
		return -1;
	}
	if(super_block.DATA_BLOCK_COUNT != block_disk_count()){
		perror("COUNT DIFFERENT\n");
		return -1;
	}
	// Initialize Root_directory
	block_read(super_block.ROOT_DIRECTORY_BLOCK, (void*)&root_directory);
	// so far skip the root directory testing, do it later.
	
	// Initialize FAT (UPDATE: we don't just put in all zeros, we have to use block_read() I think...)
	FAT = malloc(sizeof(super_block.FAT_BLOCK_COUNT * BLOCK_SIZE));
	FAT[0] = FAT_EOC;
	for(int i = 1; i < super_block.FAT_BLOCK_COUNT; i++){
		//FAT[i] = 0;
		block_read(i, (void*)&FAT[i]);
	}

	
	return 0;
}

int fs_umount(void)
{
	int write = block_write(sizeof(super_block), (void*)&super_block);
	if(write == -1){
		perror("FAIL TO WRITE\n");
		return -1;
	}

	int closeFlag = block_disk_close();
	if(closeFlag == -1){
		return -1;
	}
	int freeFlag = memFree();
	if(freeFlag != 0){
		return -1;
	}
	return 0;
}

int fs_info(void)
{
	if(disk_opened == -1){ //no underlying virtual disk was opened
		return -1;
	}
	printf("FS Info:\n");
	printf("total_blk_count=%d\n", super_block.TOTAL_BLOCKS_COUNTS);
	printf("fat_blk_count=%d\n",super_block.FAT_BLOCK_COUNT);
	printf("rdir_blk=%d\n",super_block.ROOT_DIRECTORY_BLOCK);
	printf("data_blk=%d\n",super_block.DATA_BLOCK);
	printf("data_blk_count=%d\n",super_block.DATA_BLOCK_COUNT);
	int fatFreeCounter = 0;
	for(int i = 0; i < super_block.DATA_BLOCK_COUNT; i++){
		if(FAT[i] == 0){
			fatFreeCounter++;
		}
	}
	int root_directory_cur_size = sizeof(root_directory.all_files) / sizeof(root_directory.all_files[0]);
	printf("fat_free_ratio=%d/%d\n",fatFreeCounter,super_block.DATA_BLOCK_COUNT);
	printf("rdir_free_ratio=%d/%d\n",root_directory_cur_size,FS_FILE_MAX_COUNT);
	return 0;
}

/**
 * fs_create - Create a new file
 * @filename: File name
 *
 * Create a new and empty file named @filename in the root directory of the
 * mounted file system. String @filename must be NULL-terminated and its total
 * length cannot exceed %FS_FILENAME_LEN characters (including the NULL
 * character).
 *
 * Return: -1 if @filename is invalid, if a file named @filename already exists,
 * or if string @filename is too long, or if the root directory already contains
 * %FS_FILE_MAX_COUNT files. 0 otherwise.
 */
int fs_create(const char *filename)
{
	
	int file_length = strlen(filename);
	if (filename[file_length] != '\0' || file_length > FS_FILENAME_LEN) {
		return -1;
	}

	// If file already exists
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp(filename, root_directory.all_files[i].FILENAME) == 0) { 
			return -1;
		}
	}

	// If the root directory already contains FS_FILE_MAX_COUNT files
	int root_directory_length = strlen(root_directory.all_files);
	if (root_directory_length >= FS_FILE_MAX_COUNT) {
		return -1;
	}
	
	struct file new_file;
	int new_file_index;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root_directory.all_files[i].FILENAME[0] == '\0') {
			//strcpy(root_directory.all_files[i].FILENAME, filename);
			new_file_index = i;
			break;
		}
	}

	strcpy(root_directory.all_files[new_file_index].FILENAME, filename);
	root_directory.all_files[new_file_index].FILE_SIZE = 0;
	root_directory.all_files[new_file_index].FILE_FIRST_BLOCK = FAT_EOC;

	//FAT[new_file_index] = FAT_EOC;

	return 0;

}
/**
 * fs_delete - Delete a file
 * @filename: File name
 *
 * Delete the file named @filename from the root directory of the mounted file
 * system.
 *
 * Return: -1 if @filename is invalid, if there is no file named @filename to
 * delete, or if file @filename is currently open. 0 otherwise.
 */
int fs_delete(const char *filename)
{
	if (filename == NULL) {
		return -1;
	}

	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp(filename, root_directory.all_files[i].FILENAME) != 0) { 
			return -1;
		}
	}

	// TO DO: if file @filename is currently open

	int file_index;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp(root_directory.all_files[i].FILENAME, filename) == 0) {
			file_index = i;
			break;
		}
	}

	uint16_t fat_index = root_directory.all_files[file_index].FILE_FIRST_BLOCK;
	uint16_t temp_fat_index;
	while (FAT[fat_index] != FAT_EOC) {
		temp_fat_index = FAT[fat_index];
		FAT[fat_index] = 0;
		fat_index = temp_fat_index;
	}

	strcpy(root_directory.all_files[file_index].FILENAME, "");
	root_directory.all_files[file_index].FILE_SIZE = 0;

	return 0;
}

/**
 * fs_ls - List files on file system
 *
 * List information about the files located in the root directory.
 *
 * Return: -1 if no underlying virtual disk was opened. 0 otherwise.
 */

int fs_ls(void)
{
	if(disk_opened == -1){ //no underlying virtual disk was opened
		return -1;
	}

	printf("FS Ls:\n");

	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp(root_directory.all_files[i].FILENAME, "") != 0) { // If filename is not empty, then print contents
			printf("name %s, size %d, first_data_block %d\n", root_directory.all_files[i].FILENAME, 
			root_directory.all_files[i].FILE_SIZE ,root_directory.all_files[i].FILE_FIRST_BLOCK);
		}
	}

}


int fs_open(const char *filename)
{
	// filename invalid
	int file_length = strlen(filename);
	if (filename[file_length] != '\0' || file_length > FS_FILENAME_LEN) {
		return -1;
	}
	// no filename to open
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if(strcmp(root_directory.all_files[i].FILENAME, filename) != 0){
			return -1;
		}
	}
	// max count
	if(current_open_amount == FS_OPEN_MAX_COUNT){
		return -1;
	}
	// =========
	// struct file* target_file = malloc(sizeof(struct file));
	struct file_desc* temp_file_desc = malloc(sizeof(struct file_desc));
	int fd_table_index;
	//fd_table
	//find file from root_directory
	//parse file from root to fd_table[i]
	//set file offset in fd_table to 0
	//currentopenamount++
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if(strcmp(root_directory.all_files[i].FILENAME, filename) == 0){
			temp_file_desc->cur_file = &(root_directory.all_files[i]);
			temp_file_desc->offset = 0;
			current_open_amount++;
			break;
		}
	}
	
	for(int j = 0; j < FS_OPEN_MAX_COUNT; j++){
		if(strcmp(fd_table[j], NULL) == 0){ //first empty position
			fd_table[j] = temp_file_desc;
			fd_table_index = j;
			break;
		}
	}
	free(temp_file_desc);
	//return i of fd_table[i]
	return fd_table_index;
}

int fs_close(int fd)
{
	// fd invalid out of bounds
	if(fd >= FS_OPEN_MAX_COUNT){
		return -1;
	}
	// not currently open
	if(strcmp(fd_table[fd], NULL) == 0){
		return -1;
	}
	// file close
	// set fd_table[fd] = NULL
	fd_table[fd] = NULL;
	// open amount--
	current_open_amount--;
	return 0;
}

int fs_stat(int fd)
{
	// fd invalid out of bounds
	if(fd >= FS_OPEN_MAX_COUNT){
		return -1;
	}
	// not currently open
	if(strcmp(fd_table[fd], NULL) == 0){
		return -1;
	}
	uint32_t cur_file_size;
	cur_file_size = fd_table[fd]->cur_file->FILE_SIZE;
	//retur current size of file
	return cur_file_size;
}

int fs_lseek(int fd, size_t offset)
{
	// fd invalid out of bounds
	if(fd >= FS_OPEN_MAX_COUNT){
		return -1;
	}
	// not currently open
	if(strcmp(fd_table[fd], NULL) == 0){
		return -1;
	}
	// offset larger than current file size
	if(offset > fd_table[fd]->cur_file->FILE_SIZE){
		return -1;
	}
	// set the file offset
	fd_table[fd]->offset = offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}
/**
 * fs_read - Read from a file
 * @fd: File descriptor
 * @buf: Data buffer to be filled with data
 * @count: Number of bytes of data to be read
 *
 * Attempt to read @count bytes of data from the file referenced by file
 * descriptor @fd into buffer pointer by @buf. It is assumed that @buf is large
 * enough to hold at least @count bytes.
 *
 * The number of bytes read can be smaller than @count if there are less than
 * @count bytes until the end of the file (it can even be 0 if the file offset
 * is at the end of the file). The file offset of the file descriptor is
 * implicitly incremented by the number of bytes that were actually read.
 *
 * Return: -1 if file descriptor @fd is invalid (out of bounds or not currently
 * open). Otherwise return the number of bytes actually read.
 */
int fs_read(int fd, void *buf, size_t count)
{
	size_t ret;
	// fd invalid out of bounds
	if(fd >= FS_OPEN_MAX_COUNT){
		return -1;
	}
	// not currently open
	if(strcmp(fd_table[fd], NULL) == 0){
		return -1;
	}
	struct file_desc *cur_file_desc = fd_table[fd]; //file & offset
	// Most IDEAL CASE
	// file is exactly in one block
	// which means the file is a block
	if(count == BLOCK_SIZE && cur_file_desc->cur_file->FILE_SIZE == BLOCK_SIZE){
		block_read(cur_file_desc->cur_file->FILE_FIRST_BLOCK, buf);
		ret = cur_file_desc->cur_file->FILE_SIZE;
	}
	// IDEAL CASE
	// file takes mutiple blocks (large file)
	// only read one of the blocks
	// offset is exactly at the beginning of the block
	/// offset / block_size + 1
	// index of the block
	int start_block_num = (cur_file_desc->offset / BLOCK_SIZE) + 1; // assume 3, for example
	uint16_t file_start_block_ind = cur_file_desc->cur_file->FILE_FIRST_BLOCK;
	uint16_t cur_index = start_block_num;
	for(int i = 1; i < start_block_num; i++){
		cur_index = FAT[cur_index]; 
	}
	uint16_t target_index = super_block.DATA_BLOCK + cur_index;
	block_read(target_index, buf);
	ret = BLOCK_SIZE;
	// if(cur_file_desc->offset % BLOCK_SIZE == 0 || count == BLOCK_SIZE){
	// }


	return ret;
}

