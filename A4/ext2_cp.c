#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_header.h"
FILE *src_copy; // file pointer to source file
char buffer [1024];

unsigned char *disk;

void write_file_to_blocks (int file_size, int blocks_needed, unsigned int *blocks_to_use, int block_ind , FILE *src_copy, int search_limit);

int main(int argc, char **argv) {

	char *src_path = argv[2];
    char *dst_path = argv[3];

    if(argc != 4) {
        fprintf(stderr, "Usage: %s <image file name> <directory path to file> <directory path to destination>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    //open the file for reading return error if the file doesnt exist
    src_copy = fopen(src_path,"r");
    if (src_copy == NULL){
        perror("src file not found");
        return ENOENT;
    }

    /*Important pointers: */
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2048);
    struct ext2_inode *inodes = (struct ext2_inode*)(disk + 1024 * gd->bg_inode_table);
    unsigned char *block_bits = (unsigned char *)(disk + 1024 * gd->bg_block_bitmap);
    unsigned char *inode_bits = (unsigned char *)(disk + 1024 * gd->bg_inode_bitmap);

    //Inode bitmap in the form of an array for convenience 
    int in_bitmap_index = 0;
    int inode_bitmap [sb->s_inodes_count];
    for (int byte = 0; byte < (32/8); byte++){
        for (int bit = 0; bit < 8; bit ++){
            inode_bitmap[in_bitmap_index] = (inode_bits[byte] & (1<<bit)) >> bit;
            in_bitmap_index++;
        }
    }

    //block bitmap in the form of an array for convenience 
    int bl_bitmap_index = 0;
    int block_bitmap [sb->s_blocks_count];
    for (int byte = 0; byte < (128/8); byte++){
        for (int bit = 0; bit < 8; bit ++){
            block_bitmap[bl_bitmap_index] = (block_bits[byte] & (1<<bit)) >> bit;
            bl_bitmap_index++;
        }
    }

    /*check to see if there is even an inode available to use*/
    int new_inode_num = next_available_inode(inode_bitmap);
    if (new_inode_num == -1){
        return ENOMEM;//no memory left :(
    }

    /*check to see if there is a block available for writing the contents of the file*/
    int new_block_num = next_available_block(block_bitmap);
    if (new_block_num == -1){
        return ENOMEM;// no memory left
    }


    int path_length_src = get_path_length(src_path);
    int path_length_dst = get_path_length(dst_path);

    char **src = make_path_array(src_path, path_length_src);
    char **dst = make_path_array(dst_path, path_length_dst);
    char *copy_name = src[path_length_src-1]; //only need file name
    char *temp =NULL;
    if (copy_name[strlen(copy_name)-1] == '!'){//work around to an unkown bug
        temp = malloc (strlen(copy_name)-1);
        strncpy(temp,copy_name, strlen(copy_name)-1);
        copy_name = temp;
    }


    /*Verify that the destination path is valid*/
    if (verify_path(inodes, sb, dst, path_length_dst, 0) != 0){
        return ENOENT;
    }

    /*Check if file in dst path already exists*/
    int temp_inode = 1;
    if (path_length_dst > 0){
        for (int check_ind = 0; check_ind < path_length_dst; check_ind ++){
            temp_inode = find_inode_in_blocks(inodes[temp_inode].i_block, 0, 12, dst[check_ind], 'd');
        }
    }
    //If the helper function was able to find the file then return EEXIST
    if (find_inode_in_blocks(inodes[temp_inode].i_block, 0, 12, copy_name, 'f') != -1){
        return EEXIST;
    }

    //calculate length of file in bytes
    FILE *fp = fopen(src_path, "rb");
    fseek(fp, 0, SEEK_END);
    int byte_length = ftell(fp);
    fclose(fp);

    //calculate the necessary amount of blocks needed, exit if not enough
        int blocks_needed = 0;

        if (byte_length % 1024 == 0){
            blocks_needed = byte_length / 1024;
        }else{
            blocks_needed = byte_length / 1024;
            blocks_needed ++;
        }

        if (blocks_needed > 12){
            blocks_needed ++;// indirect block
        }

        if (gd->bg_free_blocks_count < blocks_needed){
            return ENOMEM;
        }

        sb->s_free_blocks_count -= blocks_needed;
        gd->bg_free_blocks_count -= blocks_needed;
    //make a dir entry for the directory that will hold the file
    struct ext2_dir_entry new_entry;
    init_dir_entry(&new_entry, new_inode_num, strlen(copy_name), EXT2_FT_REG_FILE);
    int size = calc_rec_len_div4(&new_entry);
    int writable_block = get_writable_block(inodes[temp_inode], inodes[temp_inode].i_block, size, 0, block_bitmap, block_bits, 12, gd, sb);
    write_ent_to_block(writable_block+1, new_inode_num+1, strlen(copy_name), EXT2_FT_REG_FILE, copy_name);

    //initialize inode: no blocks have been allocated yet
    struct ext2_inode new_inode;
    init_inode(&new_inode, EXT2_S_IFREG);
    update_i_bitmap(inode_bits, new_inode_num);
    inode_bitmap[new_inode_num] = 1;
    sb->s_free_inodes_count--;
    gd->bg_free_inodes_count--;

    /*now we must fill the inode data blocks with the contents of the file*/
    new_inode.i_size = 1024 * blocks_needed;
    new_inode.i_blocks = blocks_needed * 2;
    /*Allocate blocks to the new inode*/
    int indirect_ind = 0;
    for (int k = 0; k < blocks_needed; k++){
        if (k > 12){//use indrect block
            unsigned int *indirect = (unsigned int *)(disk + (1024 * new_inode.i_block[12]));
            indirect[indirect_ind] = allocate_block(block_bitmap, block_bits) + 1;
            block_bitmap[indirect[indirect_ind] - 1] = 1;
            indirect_ind++;
        }else{
            new_inode.i_block[k] = allocate_block(block_bitmap, block_bits) + 1;
            block_bitmap[new_inode.i_block[k] - 1] = 1;
        }
    }

    inodes[new_inode_num] = new_inode;

    //write to data blocks
    write_file_to_blocks (byte_length, blocks_needed, (unsigned int *)new_inode.i_block, 0, src_copy, 12);

    if (temp != NULL){
        free(temp);
    }
    free_path(src, path_length_src);
    free_path(dst, path_length_dst);
return 0;
}

/*Copies bytes from the file to data blocks*/
void write_file_to_blocks (int file_size, int blocks_needed, unsigned int *blocks_to_use, int block_ind , FILE *src_copy, int search_limit){
    if (file_size == 0){
        return;
    }

    if(block_ind < search_limit){
        if (file_size >= 1024){
            fread (buffer, 1, 1024, src_copy);
            memcpy(disk + (1024 * blocks_to_use[block_ind]), buffer, 1024);
            write_file_to_blocks(file_size - 1024, blocks_needed, blocks_to_use, block_ind + 1, src_copy, search_limit);
        }else{
            fread (buffer, 1, file_size, src_copy);
            memcpy(disk + (1024 * blocks_to_use[block_ind]), buffer, file_size);
            return;
        }
    }else if (block_ind == 12 && search_limit == 12){
        unsigned int* indirect = (unsigned int *) (disk + (1024 * blocks_to_use[12])); 
        write_file_to_blocks(file_size, blocks_needed, indirect, 0, src_copy, blocks_needed - 12);      
    }else{
        return;
    }
}