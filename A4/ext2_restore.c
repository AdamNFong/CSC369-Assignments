
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

unsigned char *disk;

int restore_file (struct ext2_inode *dir_inode, char* file_name, struct ext2_super_block *sb, struct ext2_group_desc*gd, struct ext2_inode *inodes, int *block_bitmap, int *inode_bitmap, unsigned char *block_bits, unsigned char *inode_bits);
int check_available_block(int block_num, int *block_bitmap);
void reallocate_blocks (int search_ind, int search_limit, unsigned int *i_block, int* block_bitmap, unsigned char *block_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd);
int check_availability (int search_ind, int search_limit, unsigned int *i_block, int* block_bitmap);

int main(int argc, char **argv) {

	char *file_path = argv[2];

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <directory path to restore file or link> \n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
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

    int path_length = get_path_length(file_path);
    char **path = make_path_array(file_path, path_length); 

    /*verify path*/
    if (verify_path(inodes, sb, path, path_length, 1) != 0){
        return ENOENT;
    }

    int v_ind = 0;
    int check_inode_num = 1;

    while (v_ind < path_length -1){
        check_inode_num = find_inode_in_blocks(inodes[check_inode_num].i_block, 0, 11, path[v_ind], 'd');
        v_ind++;
    }

    //Restore file
    int status = restore_file (&inodes[check_inode_num], path[path_length - 1], sb, gd, inodes, block_bitmap, inode_bitmap, block_bits, inode_bits);

    if (status == 1 || status == 3 || status == 4){
        return ENOENT;
    }else if (status == 2){
        return EISDIR;
    }

    return 0;
}

/*Restores entry
    return 0 if successful
    return 1 if file not found
    return 2 if the file is actually a directory
    return 3 if inode is not available
    return 4 if blocks are being used
*/
int restore_file (struct ext2_inode *dir_inode, char* file_name, struct ext2_super_block *sb, struct ext2_group_desc*gd, struct ext2_inode *inodes, int *block_bitmap, int *inode_bitmap, unsigned char *block_bits, unsigned char *inode_bits){
    struct ext2_dir_entry *current_ent;
    struct ext2_dir_entry *temp;
    int rec_len_total = 0;
    int restore_rec_len = calc_rec_len_div4_name (strlen(file_name));

    // Run through dir entries and try to find gap
    for (int i = 0; i < 12; i++){
        rec_len_total = 0;
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry*)(disk + (1024 * dir_inode->i_block[i]) + rec_len_total);
            int actual_rec_len = calc_rec_len_div4(current_ent);
            if ((current_ent->rec_len - actual_rec_len) >= restore_rec_len){ //check for gap
                temp = (struct ext2_dir_entry*)(disk + (1024 * dir_inode->i_block[i]) + rec_len_total + actual_rec_len);
                if (strncmp(temp->name, file_name, strlen(file_name)) == 0){
                    if (dir_type(temp->file_type) == 'd'){
                        return 2;
                    }else if (dir_type(temp->file_type) == 'f' || dir_type(temp->file_type) == 'l'){

                        //inode in use already
                        if (inode_bitmap[temp->inode - 1] == 1){
                            return 3;
                        }

                        if(temp->inode == 0){
                            return 3; //Can't retrieve previous inode num
                        }

                        unsigned int* blocks = inodes[temp->inode - 1].i_block;  
                        update_i_bitmap (inode_bits, temp->inode - 1);
                        inode_bitmap[temp->inode -1] = 1;
                        inodes[temp->inode - 1].i_links_count++;

                        sb->s_free_inodes_count --;
                        gd->bg_free_inodes_count --;
                        //check if blocks are available
                        if (check_availability(0, 12, blocks, block_bitmap) == 1){
                            return 4;
                        }

                        //reallocate blocks
                        reallocate_blocks(0, 12, blocks, block_bitmap, block_bits, sb, gd);

                        //Adjust rec_len
                        int new_rec_len = current_ent->rec_len - actual_rec_len;
                        current_ent->rec_len = actual_rec_len;
                        temp->rec_len = new_rec_len;
                        return 0;

                    }
                }
            }
            rec_len_total += current_ent->rec_len;
        }
    }
    return 1;
}

//check if block is already in use
int check_available_block(int block_num, int *block_bitmap){
    if (block_bitmap[block_num -1] == 0){
        return 0;
    }else{
        return 1;
    }
}

//reallocate blocks to removed entry
void reallocate_blocks (int search_ind, int search_limit, unsigned int *i_block, int* block_bitmap, unsigned char *block_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd){
    if (i_block [search_ind] != 0){
        if (search_ind < search_limit){
            update_b_bitmap(block_bits, i_block[search_ind] - 1);
            block_bitmap[i_block[search_ind]-1] = 1;
            gd->bg_free_blocks_count--;
            sb->s_free_blocks_count--;
            reallocate_blocks(search_ind + 1, search_limit, i_block, block_bitmap, block_bits, sb, gd);
        }else{
            if (search_limit != 200){
                update_b_bitmap(block_bits, i_block[12] - 1);
                block_bitmap[i_block[12]-1] = 1; 
                gd->bg_free_blocks_count--;
                sb->s_free_blocks_count--;
                unsigned int *indirect = (unsigned int*)(disk + (1024*i_block[12]));
                reallocate_blocks(0, 200, indirect, block_bitmap, block_bits, sb, gd);
            }else{
                return; //done allocating blocks
            }
        }
    }else{
        return;
    }
}

//check if all the blocks are in use
int check_availability (int search_ind, int search_limit, unsigned int *i_block, int* block_bitmap){
    if (i_block [search_ind] != 0){
        if (search_ind < search_limit){
            if (check_available_block(i_block[search_ind], block_bitmap) == 1){
                return 1;
            }
            return check_availability(search_ind + 1, search_limit, i_block, block_bitmap);
        }else{
            if (search_limit != 200){
                if (check_available_block(i_block[12], block_bitmap) == 1){
                    return 1;
                }
                unsigned int *indirect = (unsigned int*)(disk + (1024*i_block[12]));
                return check_availability(0, 200, indirect, block_bitmap);
            }else{
                return 0;// indirect block has been searched: no errors
            }
        }
    }else{
        return 0;
    }
} 