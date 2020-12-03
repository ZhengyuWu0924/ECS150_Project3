#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

struct SuperBlock{
	uint8_t SIGNATURE[8]; // ECS150FS
	uint16_t TOTAL_BLOCKS_COUNTS; // Total # of blocks
	uint16_t ROOT_DIRECTORY_BLOCK; // Root directory index
	uint16_t DATA_BLOCK; // First data block index
	uint16_t DATA_BLOCK_COUNT; // # of data blocks
	uint8_t FAT_BLOCK_COUNT; // # of FAT blocks
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
	uint16_t FILE_FIRST_BLOCK; // Index of the first data block for the file
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
struct SuperBlock* super_block;
struct RootDirectory* root_directory;
struct file_desc *fd_table[FS_OPEN_MAX_COUNT];
int disk_opened;
int current_open_amount;

int memFree(void){
	free(FAT);
	free(super_block);
	free(root_directory);
	return 0;
}

int signature_cmp(uint8_t* sig, char* target, int len){
	for(int i = 0; i < len; i++){
		if(sig[i] != (uint8_t)target[i]){
			return -1;
		}
	}
	return 0;
}
//=====
int fs_mount(const char *diskname)
{
	int disk_opened = block_disk_open(diskname);
	if(disk_opened == -1){
		//perror("Fail to open disk");
		return -1;
	}
	// printf("Ok - 1\n");
	super_block = malloc(sizeof(struct SuperBlock));
	block_read(0, super_block);

	if(strncmp((char*)super_block->SIGNATURE, "ECS150FS", 8) != 0){
		return -1;
	}

	if(super_block->TOTAL_BLOCKS_COUNTS != block_disk_count()){
		return -1;
	}
	
	// Initialize Root_directory
	// printf("Ok - 2\n");
	root_directory = malloc(BLOCK_SIZE);
	block_read(super_block->ROOT_DIRECTORY_BLOCK, root_directory);

	// so far skip the root directory testing, do it later.
	// Initialize FAT (UPDATE: we don't just put in all zeros, we have to use block_read() I think...)
	// printf("Ok - 3\n");
	FAT = malloc(super_block->FAT_BLOCK_COUNT * BLOCK_SIZE); // assign memory space for BLOCK_SIZE of table
	//FAT[0] = FAT_EOC;
	for(int i = 0; i < super_block->FAT_BLOCK_COUNT; i++){
		//FAT[i] = 0;
		block_read(i + 1, FAT + (i * (BLOCK_SIZE/sizeof(uint16_t)))); // WHAT DOES THIS MEAN?!
	}
	// for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
	// 	root_directory->all_files[i].FILENAME[0] = '\0';
	// 	root_directory->all_files[i].FILE_SIZE = 0;
	// 	root_directory->all_files[i].FILE_FIRST_BLOCK = FAT_EOC;
	// }
	return 0;
}

int fs_umount(void)
{
	// int write = block_write(sizeof(super_block), super_block);
	// if(write == -1){
	// 	perror("FAIL TO WRITE\n");
	// 	return -1;
	// }

	// for(int i = 0; i < super_block->FAT_BLOCK_COUNT; i++){
	// 	//FAT[i] = 0;
	// 	block_write(i + 1, FAT + (i * (BLOCK_SIZE/sizeof(uint16_t))));
	// }
	
	// block_write(super_block->ROOT_DIRECTORY_BLOCK, root_directory);

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
	if(super_block == NULL || root_directory == NULL || FAT == NULL){ //no underlying virtual disk was opened
		return -1;
	}
	printf("FS Info:\n");
	printf("total_blk_count=%d\n", super_block->TOTAL_BLOCKS_COUNTS);
	printf("fat_blk_count=%d\n",super_block->FAT_BLOCK_COUNT);
	printf("rdir_blk=%d\n",super_block->ROOT_DIRECTORY_BLOCK);
	printf("data_blk=%d\n",super_block->DATA_BLOCK);
	printf("data_blk_count=%d\n",super_block->DATA_BLOCK_COUNT);
	int fatFreeCounter = 0;
	for(int i = 0; i < super_block->DATA_BLOCK_COUNT; i++){
		if(FAT[i] == 0){
			fatFreeCounter++;
		}
	}
	int root_directory_free_size = 0;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(root_directory->all_files[i].FILENAME[0] == '\0'){
			root_directory_free_size++;
		}
	}
	// int root_directory_cur_size = sizeof(root_directory->all_files) / sizeof(root_directory->all_files[0]);
	printf("fat_free_ratio=%d/%d\n",fatFreeCounter,super_block->DATA_BLOCK_COUNT);
	printf("rdir_free_ratio=%d/%d\n",root_directory_free_size,FS_FILE_MAX_COUNT);
	return 0;
}

int fs_create(const char *filename)
{
	if(filename == NULL){
		return -1;
	}
	int file_length = strlen(filename);
	
	// if (filename[file_length] != '\0' || file_length > FS_FILENAME_LEN) {
	// 	return -1;
	// }
	if(file_length >= FS_FILENAME_LEN){
		return -1;
	}
	// If file already exists
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp(filename, (char*)root_directory->all_files[i].FILENAME) == 0) { 
			return -1;
		}
	}
	// If the root directory already contains FS_FILE_MAX_COUNT files
	// sizeof(msg)/sizeof(uint8_t);
	int root_directory_length = 0;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(root_directory->all_files[i].FILENAME[0] != '\0'){
			root_directory_length += 1;;
		}
	}
	// int root_directory_length = sizeof(root_directory->all_files)/sizeof(root_directory->all_files[0]);
	// int root_directory_length = strlen(root_directory.all_files);
	if (root_directory_length >= FS_FILE_MAX_COUNT) {
		return -1;
	}
	// struct file new_file;
	int new_file_index;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root_directory->all_files[i].FILENAME[0] == '\0') { // first available in rootdirectory
			//strcpy(root_directory.all_files[i].FILENAME, filename);
			new_file_index = i;
			break;
		}
	}
	// for(int i = 0; i < file_length; i++){
	// 	strcpy(root_directory.all_files[new_file_index].FILENAME[i], (uint8_t)filename[i]);
	// }
	memcpy(root_directory->all_files[new_file_index].FILENAME, filename, file_length+1);
	root_directory->all_files[new_file_index].FILE_SIZE = 0;
	root_directory->all_files[new_file_index].FILE_FIRST_BLOCK = FAT_EOC;
	block_write(super_block->ROOT_DIRECTORY_BLOCK, root_directory);
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
	int errFlag = -1;
	if (filename == NULL) {
		//printf("1st if\n");
		errFlag = -1;
	}

	// If file not exist
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp(filename, (char*)root_directory->all_files[i].FILENAME) == 0) {
			errFlag = 0;
		}
	}
	if(errFlag == -1){
		//printf("error flag\n");
		return -1;
	}

	// TO DO: if file @filename is currently open

	int file_index;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp((char*)root_directory->all_files[i].FILENAME, filename) == 0) {
			file_index = i;
			break;
		}
	}
	//printf("start while loop\n");
	uint16_t fat_index = root_directory->all_files[file_index].FILE_FIRST_BLOCK;
	uint16_t temp_fat_index;
	if(fat_index != FAT_EOC){
		while (FAT[fat_index] != FAT_EOC) {
			temp_fat_index = FAT[fat_index];
			FAT[fat_index] = 0;
			fat_index = temp_fat_index;
		}
	}
	//printf("above rootDirectory\n");
	root_directory->all_files[file_index].FILENAME[0] = '\0';
	root_directory->all_files[file_index].FILE_SIZE = 0;
	//printf("above rdir block write\n");
	block_write(super_block->ROOT_DIRECTORY_BLOCK, root_directory);
	//printf("after rdir write\n");
	for(int i = 1; i <= super_block->FAT_BLOCK_COUNT; i++){
		// block_write(i + 1, FAT + ((i-1) * (BLOCK_SIZE/2)));
	}
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
	// printf("entering ls\n");
	if(disk_opened == -1){ //no underlying virtual disk was opened
		return -1;
	}

	printf("FS Ls:\n");

	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root_directory->all_files[i].FILENAME[0] != '\0') { // If filename is not empty, then print contents
			printf("file: %s, size: %d, data_blk: %d\n", root_directory->all_files[i].FILENAME, 
			root_directory->all_files[i].FILE_SIZE ,root_directory->all_files[i].FILE_FIRST_BLOCK);
		}
	}
	return 0;
}


int fs_open(const char *filename)
{
	int returnFlag = -1;
	// filename invalid
	int file_length = strlen(filename);
	if (filename[file_length] != '\0' || file_length > FS_FILENAME_LEN) {
		// printf("return at first if in open\n");
		returnFlag = -1;
	}
	// no filename to open
	// printf("max count: %d\n", FS_FILE_MAX_COUNT);
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if(strcmp((char*)root_directory->all_files[i].FILENAME, filename) == 0){
			// printf("FILENAME:%s\n",root_directory->all_files[i].FILENAME);
			// printf("filename:%s\n",filename);
			// printf("return at 2nd if in open\n");
			returnFlag = 0;
			break;
		}
	}
	
	// max count
	if(current_open_amount == FS_OPEN_MAX_COUNT){
		// printf("return at 3rd if in open\n");
		returnFlag = -1;
	}
	if(returnFlag == -1){
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
		if(strcmp((char*)root_directory->all_files[i].FILENAME, filename) == 0){
			// printf("first assign value to file desc\n");
			temp_file_desc->cur_file = &(root_directory->all_files[i]);
			// printf("root_directory->all_files:%s\n", root_directory->all_files[i].FILENAME);
			// printf("temp_file_desc->curfile:%s\n",(char*)temp_file_desc->cur_file);
			// printf("assign temp file desc offset\n");
			temp_file_desc->offset = 0;
			// printf("increase current open amount\n");
			current_open_amount++;
			break;
		}
	}
	
	for(int j = 0; j < FS_OPEN_MAX_COUNT; j++){
		if(fd_table[j] == NULL){ //first empty position
			// printf("finding first available position in  fd_table\n");
			fd_table[j] =temp_file_desc;
			// memcpy(fd_table[j], temp_file_desc, sizeof(temp_file_desc));
			// strcpy(fd_table[j],temp_file_desc);
			// printf("assign fdtable[j] = %s\n",fd_table[j]->cur_file->FILENAME);
			// printf("current fdtable[j] offset = %d\n", fd_table[j]->offset);
			fd_table_index = j;
			// printf("fd table index = %d\n", fd_table_index);
			break;
		}
	}
	// free(temp_file_desc);
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
	if(fd_table[fd] == NULL){
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
	// printf("fd = %d\n", fd);
	// printf("fd info = %s\n", fd_table[fd]->cur_file->FILENAME);
	if(fd >= FS_OPEN_MAX_COUNT){
		return -1;
	}
	// not currently open
	if(fd_table[fd] == NULL){
		return -1;
	}
	uint32_t cur_file_size;
	cur_file_size = fd_table[fd]->cur_file->FILE_SIZE;
	//return current size of file
	return cur_file_size;
}

int fs_lseek(int fd, size_t offset)
{
	// fd invalid out of bounds
	if(fd >= FS_OPEN_MAX_COUNT){
		return -1;
	}
	// not currently open
	if(fd_table[fd] == NULL){
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
int index_containing_offset (struct file_desc* cur_file_desc, int first_block) {
	int start_block_num = (cur_file_desc->offset / BLOCK_SIZE); // assume 3, for example
	// int start_block_num = (cur_file_desc->cur_file->FILE_FIRST_BLOCK % BLOCK_SIZE);
	
	// uint16_t file_start_block_ind = cur_file_desc->cur_file->FILE_FIRST_BLOCK;
	// uint16_t cur_index = start_block_num;
	// for(int i = 1; i < start_block_num; i++){
	// 	cur_index = FAT[cur_index]; 
		
	// }
	// uint16_t target_index = super_block->DATA_BLOCK + cur_index;
	uint16_t target_index = start_block_num + first_block + super_block->DATA_BLOCK;
	//printf("target_index = %d\n", target_index);
	return target_index;
}

// get_new_block_index() returns the first available data block index. If no data block available, returns -1
int get_new_block_index() {
	int new_block_index = -1;
	for (int i = 0; i < super_block->DATA_BLOCK_COUNT; i++) {
		if (FAT[i] == 0) { // if data block is free (rep as 0 in prompt)
			new_block_index = i;
			break;
		}
	}
	return new_block_index;
}

/**
 * fs_write - Write to a file
 * @fd: File descriptor
 * @buf: Data buffer to write in the file
 * @count: Number of bytes of data to be written
 *
 * Attempt to write @count bytes of data from buffer pointer by @buf into the
 * file referenced by file descriptor @fd. It is assumed that @buf holds at
 * least @count bytes.
 *
 * When the function attempts to write past the end of the file, the file is
 * automatically extended to hold the additional bytes. If the underlying disk
 * runs out of space while performing a write operation, fs_write() should write
 * as many bytes as possible. The number of written bytes can therefore be
 * smaller than @count (it can even be 0 if there is no more space on disk).
 *
 * Return: -1 if file descriptor @fd is invalid (out of bounds or not currently
 * open). Otherwise return the number of bytes actually written.
 */

int fs_write(int fd, void *buf, size_t count) {
	// Error Management
	if (fd >= FS_OPEN_MAX_COUNT || fd < 0) { // fd out of bounds
		return -1;
	}
	if (fd_table[fd] == NULL) { // if file not currently open
		return -1;
	}
	if (count < 0) {
		return -1;
	}

	// Get the file using fd_table
	struct file_desc *cur_file_desc = fd_table[fd];

	// If the file is empty, assign data block and update FAT linking
	if (cur_file_desc->cur_file->FILE_FIRST_BLOCK == FAT_EOC) {
		int new_block_index = get_new_block_index();
		if (new_block_index == -1){
			return 0; // No more blocks available, so we couldn't write any bytes. Therefore, we wrote 0 bytes. Return 0.
		}
		cur_file_desc->cur_file->FILE_FIRST_BLOCK = new_block_index;
		FAT[new_block_index] = FAT_EOC;
	}

	// Get index position of the first data block of the file
	int first_block_pos = 0;
	for (int i = 0 ; i < FS_FILE_MAX_COUNT; i++){
		if (root_directory->all_files[i].FILENAME == cur_file_desc->cur_file->FILENAME){ // found the file
			first_block_pos = root_directory->all_files[i].FILE_FIRST_BLOCK;
		}
	}

	int remaining_to_write = count;
	char* bounce = malloc(BLOCK_SIZE);
	int buffer_offset = 0; // Keep track of how much of the buffer we already wrote into disk
	while (remaining_to_write > 0){
		int target_index = index_containing_offset(cur_file_desc, first_block_pos); // block index which contains the offset
		int block_offset = cur_file_desc->offset % BLOCK_SIZE; // We know how far in we are into this block
		block_read(target_index, bounce); // Read entire block into bounce

		// 3 cases 
		//	1. if (block_offset + remaining_to_write < BLOCK_SIZE) ==> We can fit the data into the block
		//	2. if (block_offset + remaining_to_write >= BLOCK SIZE) ==> Can't fit into block. 

		if (block_offset + remaining_to_write < BLOCK_SIZE){ // We can fit the data into this block
			memcpy(bounce+block_offset, buf+buffer_offset, remaining_to_write);
			cur_file_desc->offset += remaining_to_write;
			buffer_offset += remaining_to_write;
			remaining_to_write = 0; // Wrote the small buf case into block. Nothing more to read.
			block_write(target_index, bounce);
		}
		// Can't fit entire data in the block. Fit as much as we can in this block, then create a new data_block, do linking,
		// then put remaining into that block
		else if (block_offset + remaining_to_write > BLOCK_SIZE) { 
			int block_left = BLOCK_SIZE - block_offset;
			memcpy(bounce+block_offset, buf, block_left);
			cur_file_desc->offset += block_left;
			buffer_offset += block_left;
			remaining_to_write = remaining_to_write - block_left;
			// Now, copy the bounce into the disk (as much as we have read)
			block_write(target_index, bounce);
			// So far, we read as much as we could into the block. Now, assign new data block and link with FAT. Then add data into new block.

			int new_block_index = get_new_block_index();
			if (new_block_index == -1) { // We wrote as much as possible. We don't have any more space. Just return.
				return (count - remaining_to_write);
			}
			FAT[target_index] = new_block_index; // Update FAT to link to new block
			FAT[new_block_index] = FAT_EOC; // Added a new data block to the file. This one is now the end of the file.

		}
		else if (block_offset + remaining_to_write == BLOCK_SIZE){
			memcpy(bounce+block_offset, buf+buffer_offset, remaining_to_write);
			cur_file_desc->offset += remaining_to_write;
			buffer_offset += remaining_to_write;
			remaining_to_write = remaining_to_write - BLOCK_SIZE;
		}
	}
	return count;
}

int fs_read(int fd, void *buf, size_t count)
{
	// fd invalid out of bounds
	if(fd >= FS_OPEN_MAX_COUNT){
		return -1;
	}
	// not currently open
	if(fd_table[fd] == NULL){
		return -1;
	}
	int first_block_pos = 0;
	struct file_desc *cur_file_desc = fd_table[fd]; //file & offset
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(root_directory->all_files[i].FILENAME == cur_file_desc->cur_file->FILENAME){
			first_block_pos = root_directory->all_files[i].FILE_FIRST_BLOCK;
		}
	}

	int remaining_to_read = count;
	// BEGINNING
	char* bounce = malloc(BLOCK_SIZE);
	int buffer_offset = 0; // We are adding data in pieces, so we need to keep track of beginning of buffer
	while (remaining_to_read > 0) { // Loop until we have no more bytes to read 
		int target_index = index_containing_offset(cur_file_desc, first_block_pos);
		block_read(target_index, bounce);
		int block_offset = cur_file_desc->offset % BLOCK_SIZE; // Shows how far in we are into the block

		if (block_offset + remaining_to_read < BLOCK_SIZE) { // CASE 1: We extract part of the block (where offset is in the middle, count is the end)
			memcpy(buf + buffer_offset, bounce + block_offset, remaining_to_read);
			cur_file_desc->offset += remaining_to_read;
			buffer_offset += remaining_to_read;
			remaining_to_read = 0; // Ensures that the loop won't run again
			
		}
		else if (block_offset + remaining_to_read > BLOCK_SIZE) { // CASE 2: Bigger than a block
			int block_left = BLOCK_SIZE - block_offset;
			memcpy(buf + buffer_offset, bounce + block_offset, block_left);
			cur_file_desc->offset += block_left;
			buffer_offset += block_left;
			remaining_to_read = remaining_to_read - block_left;

		}
		else if ((block_offset + remaining_to_read) == BLOCK_SIZE) { // CASE 3: We're reading exactly a block (IDEAL CASE)
			memcpy(buf + buffer_offset, bounce + block_offset, remaining_to_read);
			cur_file_desc->offset += remaining_to_read;
			buffer_offset += remaining_to_read;
			remaining_to_read = remaining_to_read - BLOCK_SIZE;
		}
	}
	
	return count; // CHECK THIS
}


// int fs_write(int fd, void *buf, size_t count)
// {
// 	// Error Management
// 	// fd invalid out of bounds
// 	if(fd >= FS_OPEN_MAX_COUNT){
// 		return -1;
// 	}
// 	// not currently open
// 	if(fd_table[fd] == NULL){
// 		return -1;
// 	}
// 	int first_block_pos = 0;
// 	struct file_desc *cur_file_desc = fd_table[fd]; //file & offset
// 	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
// 		if(root_directory->all_files[i].FILENAME == cur_file_desc->cur_file->FILENAME){
// 			first_block_pos = root_directory->all_files[i].FILE_FIRST_BLOCK;
// 		}
// 	}
// 	int data_record = 0;
// 	char* bounce = malloc(BLOCK_SIZE); // bounce block to transit data

// 	int remaining_to_read = count; // data to be written
// 	// struct file_desc *cur_file_desc = fd_table[fd]; // current file to be written
// 	int size_to_write = 0;
// 	int buffer_offset = 0;
// 	while(remaining_to_read > 0){
// 		int target_block = index_containing_offset(cur_file_desc, first_block_pos); // find the block the file offset in
// 		int block_offset = cur_file_desc->offset % BLOCK_SIZE; // get the position in the block 
// 		block_read(target_block, bounce);
// 		if(block_offset + remaining_to_read <= cur_file_desc->cur_file->FILE_SIZE){ // Current data + things to be written can exactly filled a block
// 		/**
// 		 * 	memcpy(buf + buffer_offset, bounce + block_offset, remaining_to_read);
// 			cur_file_desc->offset += remaining_to_read;
// 			buffer_offset += remaining_to_read;
// 			remaining_to_read = 0; // Ensures that the loop won't run again
			
// 		 * */
// 			size_to_write = remaining_to_read;
// 			// block_read(target_block + block_offset, bounce); // read from current index in the block to the bounce
// 			memcpy(bounce + block_offset, buf + buffer_offset, size_to_write); // copy remaining bytes of data from buf to bounce start at [current index]
			
// 			block_write(target_block + block_offset, bounce); // write data start at block_offset in bounce;
// 			data_record += size_to_write;
// 			remaining_to_read = 0;
// 		} else if (block_offset + remaining_to_read > cur_file_desc->cur_file->FILE_SIZE){
// 			int minBytes = 0;
// 			if(remaining_to_read > cur_file_desc->cur_file->FILE_SIZE){
// 				minBytes = cur_file_desc->cur_file->FILE_SIZE;
// 			} else {
// 				minBytes = remaining_to_read;
// 			}
// 			size_to_write = minBytes;
// 			// block_read(target_block + block_offset, bounce);
// 			memcpy(bounce + block_offset, buf + buffer_offset, size_to_write);
// 			block_write(target_block + block_offset, bounce);
// 			data_record += size_to_write;
// 			remaining_to_read -= size_to_write;
// 		}
// 	}
// 	return data_record;
// }








