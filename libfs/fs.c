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

uint16_t *FAT;
struct SuperBlock super_block;
struct RootDirectory root_directory;
int disk_opened;

int memFree(void){
	free(FAT);
	free((void*)&super_block);
	free((void*)&root_directory);
	return 0;
}

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
	printf("fat_free_ratio=%d/%d\n",fatFreeCounter,super_block.DATA_BLOCK_COUNT);
	printf("rdir_free_ratio=%d/%d\n",root_directory.FILE_SIZE,FS_FILE_MAX_COUNT);
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
		if (strcmp(filename, root_directory.all_files[i]) == 0) { 
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
		if (strcmp(filename, root_directory.all_files[i]) != 0) { 
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
	/* TODO: Phase 3 */
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

