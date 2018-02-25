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

int abs_value (int value);
void inode_check (int *inode_bitmap, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *total);
void block_check (int *block_bitmap, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *total);
void i_mode_check(unsigned int* i_block, int ind, int *total, struct ext2_inode *inodes);
void inode_all_check(unsigned int *i_block, int ind, int *total, int *inode_bitmap, unsigned char *inode_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, struct ext2_inode *inodes);
void block_all_check(unsigned int *i_block, int ind, int *total, int *block_bitmap, unsigned char *block_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, int count, int inode_num, struct ext2_inode *inodes);
void file_block_check (unsigned int *i_block, int ind, int search_limit, int *total,int *block_bitmap, unsigned char *block_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, int count, int inode_num);
void i_dtime_check(unsigned int* i_block, int ind, int *total, struct ext2_inode *inodes);
unsigned char inode_to_dir_type (unsigned short i_mode);
int check_i_bitmap(int *inode_bitmap, int inode_num);
int check_b_bitmap(int *block_bitmap, int block_num);

int main(int argc, char **argv) {

    int total_fixes = 0;
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name> \n", argv[0]);
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

    /*a.*/
    inode_check (inode_bitmap, sb, gd, &total_fixes);
    block_check (block_bitmap, sb, gd, &total_fixes);   

    /*b.*/
    i_mode_check (inodes[2].i_block, 0, &total_fixes, inodes);

    /*c.*/
    inode_all_check(inodes[2].i_block, 0, &total_fixes, inode_bitmap, inode_bits, sb, gd, inodes);

    /*d.*/
    i_dtime_check(inodes[2].i_block, 0, &total_fixes, inodes);

    /*e.*/
    block_all_check(inodes[2].i_block, 0, &total_fixes, block_bitmap, block_bits, sb, gd, 0, 2, inodes);

    if (total_fixes != 0){
        printf("%d file system inconsistencies repaired!\n", total_fixes);
    }else{
        printf("No file system inconsistencies detected!\n");
    }

return 0;
}

//returns absolute value
int abs_value (int value){
    if (value < 0){
        return -value;
    }else{
        return value;
    }
}

//for checking the free_inodes counter
void inode_check (int *inode_bitmap, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *total){
    int temp_count = 0;
    int diff = 0;
    //count the available inodes in bitmap
    for (int i = 0; i < 32; i++){
        if (inode_bitmap[i] == 0){
            temp_count ++;
        }
    }
    //if the amount does not match the sb fix and record
    if (temp_count != sb->s_free_inodes_count){
        diff = temp_count - sb->s_free_inodes_count;
        if (diff < 0){
            sb->s_free_inodes_count -=diff;
        }else{
            sb->s_free_inodes_count +=diff;
        }
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", abs_value(diff)); 
        *total += abs_value(diff);
    }

    //if the amount does not match the gd fix and record
    if (temp_count != gd->bg_free_inodes_count){
        diff = temp_count - gd->bg_free_inodes_count;
        if (diff < 0){
            gd->bg_free_inodes_count -=diff;
        }else{
            gd->bg_free_inodes_count +=diff;
        }
        *total += abs_value(diff);
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", abs_value(diff)); 
    }
}
//for checking free block counters, virtually the same as inode_check
void block_check (int *block_bitmap, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *total){  
    int temp_count = 0;
    int diff = 0;
    for (int i = 0; i < 128; i++){
        if (block_bitmap[i] == 0){
            temp_count ++;
        }
    }

    if (temp_count != sb->s_free_blocks_count){
        diff = temp_count - sb->s_free_blocks_count;
        if (diff < 0){
            sb->s_free_inodes_count -=diff;
        }else{
             sb->s_free_inodes_count +=diff;
        }
        *total += abs_value(diff);
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", abs_value(diff)); 
    }

    if (temp_count != gd->bg_free_blocks_count){
        diff = temp_count - gd->bg_free_blocks_count;
        if (diff < 0){
            gd->bg_free_inodes_count -=diff;
        }else{
            gd->bg_free_inodes_count +=diff;
        }
        *total += abs_value(diff);
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", abs_value(diff)); 
    }   
}

//checks if the dir entry file type matches the inode file type (depth-first traversal)
void i_mode_check(unsigned int* i_block, int ind, int *total, struct ext2_inode *inodes){
    struct ext2_dir_entry *current_ent;
    int rec_len_total;

    if (i_block[ind] != 0){
        int block_num = i_block[ind] + 1;
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
            struct ext2_inode insp_inode = inodes [current_ent->inode - 1];
            if (strncmp("..",current_ent->name,2) != 0 && strncmp(".",current_ent->name,1) != 0){//don't want to accidently infinitely recurse back and forth between . and .. dirs
                if (f_type(insp_inode.i_mode) != dir_type(current_ent->file_type)){
                    current_ent->file_type = inode_to_dir_type (insp_inode.i_mode);
                    printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", current_ent->inode);
                    (*total)++;
                }
                if (dir_type(current_ent->file_type) == 'd'){
                    i_mode_check(insp_inode.i_block, 0 ,total, inodes);//recurse an check the entries in this directory
                }
            }
            rec_len_total += current_ent->rec_len;
        }
        i_mode_check(i_block, ind + 1, total, inodes);//check the entries in the next block
    }else{
        return;
    }
}

//checks if the inodes are marked as in use (depth-first traversal)
void inode_all_check(unsigned int *i_block, int ind, int *total, int *inode_bitmap, unsigned char *inode_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, struct ext2_inode *inodes){
    struct ext2_dir_entry *current_ent;
    int rec_len_total = 0;

    if (i_block[ind] != 0){
        int block_num = i_block[ind];
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
            struct ext2_inode insp_inode = inodes [current_ent->inode - 1];
            if (strncmp("..",current_ent->name,2) != 0 && strncmp(".",current_ent->name,1) != 0){
                if (check_i_bitmap(inode_bitmap, current_ent->inode -1) == 0){
                    update_i_bitmap(inode_bits,current_ent->inode -1);
                    inode_bitmap[current_ent->inode-1] = 1;
                    sb->s_free_inodes_count--;
                    gd->bg_free_inodes_count--;
                    printf("Fixed: inode [%d] not marked as in-use\n", current_ent->inode);
                    (*total)++;
                }
                if (dir_type(current_ent->file_type) == 'd'){
                    inode_all_check(insp_inode.i_block, 0, total, inode_bitmap, inode_bits, sb, gd, inodes);// if its a directory recurse
                }
            }
            rec_len_total += current_ent->rec_len;
        }
        inode_all_check(i_block, ind + 1, total, inode_bitmap, inode_bits, sb, gd, inodes);//check next block
    }else{
        return;
    }
}

//checks to see if i_dtime is set for any inodes (depth-first traversal)
void i_dtime_check(unsigned int* i_block, int ind, int *total, struct ext2_inode *inodes){
    struct ext2_dir_entry *current_ent;
    int rec_len_total;

    if (i_block[ind] != 0){
        int block_num = i_block[ind] + 1;
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
            struct ext2_inode insp_inode = inodes [current_ent->inode - 1];
            if (strncmp("..",current_ent->name,2) != 0 && strncmp(".",current_ent->name,1) != 0){
                if (insp_inode.i_dtime == 0){
                    insp_inode.i_dtime = 0;
                    printf("Fixed: valid inode marked for deletion: [%d]\n", current_ent->inode);
                    (*total)++;
                }
                if (dir_type(current_ent->file_type) == 'd'){
                    i_dtime_check(insp_inode.i_block, 0 ,total, inodes);
                }
            }
            rec_len_total += current_ent->rec_len;
        }
        i_dtime_check(i_block, ind + 1, total, inodes);
    }else{
        return;
    }
}

//changes inode i_mode into appropriate dir_entry file_type
unsigned char inode_to_dir_type (unsigned short i_mode){
    switch ((i_mode>>12)<<12){
        case EXT2_S_IFLNK:
            return EXT2_FT_SYMLINK; //link
        case EXT2_S_IFREG:
            return EXT2_FT_REG_FILE; //file
        case EXT2_S_IFDIR:
            return EXT2_FT_DIR; //directory
        default:   
            return EXT2_FT_REG_FILE;//error
    }
}

//checks if the block are marked as in use (depth-first traversal)
void block_all_check(unsigned int *i_block, int ind, int *total, int *block_bitmap, unsigned char *block_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, int count, int inode_num, struct ext2_inode *inodes){
    struct ext2_dir_entry *current_ent;
    int rec_len_total = 0;
    int c = count;
    if (i_block[ind] != 0){
        if (check_b_bitmap (block_bitmap,i_block[ind]-1)==0){ //check if block is in use
            update_b_bitmap(block_bits, i_block[ind]-1);
            block_bitmap[i_block[ind]-1] = 1;
            sb->s_free_blocks_count--;
            gd->bg_free_blocks_count--;
            c ++;
            (*total)++;
        }
        int block_num = i_block[ind];
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
            struct ext2_inode insp_inode = inodes [current_ent->inode - 1];
            if (strncmp("..",current_ent->name,2) != 0 && strncmp(".",current_ent->name,1) != 0){
                if (dir_type(current_ent->file_type) == 'f' || dir_type(current_ent->file_type) == 'l'){
                    file_block_check(insp_inode.i_block, 0, 12, total, block_bitmap, block_bits, sb, gd, 0, current_ent->inode); //check if files blocks are marked as in use
                }

                if (dir_type(current_ent->file_type) == 'd'){
                    block_all_check(insp_inode.i_block, 0 ,total, block_bitmap, block_bits, sb, gd, 0, current_ent->inode, inodes); //recurse if its a directory
                }
            }
            rec_len_total += current_ent->rec_len;
        }
        block_all_check(i_block, ind + 1, total, block_bitmap, block_bits, sb, gd, c, inode_num, inodes); //check next dir_entry block
    }else{
        if (c > 0){ //once you reach the last dir_entry in use block report the changes 
            printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", c, inode_num);
        }
        return; //if there were no changes then just return, nothing to report
    }
}

//helper function to block_all_check that will check the blocks of a file or symlink and report it
void file_block_check (unsigned int *i_block, int ind, int search_limit, int *total,int *block_bitmap, unsigned char *block_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd, int count, int inode_num){
    int c = count;
    if (i_block[ind] != 0){
        if (check_b_bitmap (block_bitmap,i_block[ind]-1)==0){
            update_b_bitmap(block_bits, i_block[ind]-1);
            block_bitmap[i_block[ind]-1] = 1;
            sb->s_free_blocks_count--;
            gd->bg_free_blocks_count--;
            c++;
            (*total)++;
        }
        if (ind < search_limit){
            file_block_check (i_block, ind + 1, search_limit, total, block_bitmap, block_bits, sb, gd, c, inode_num);
        }else if (search_limit == 12){
            unsigned int *indirect = (unsigned int *)(disk + (1024 * i_block[12]));
            file_block_check (indirect, 0, 200, total, block_bitmap, block_bits, sb, gd, c, inode_num);
        }else{
            if (c > 0){
                printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", c, inode_num);
            }   
            return;  
        }
    }else{
        if (c > 0){
            printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", c, inode_num);
        }   
        return;
    }
}

int check_i_bitmap(int *inode_bitmap, int inode_num){
    return inode_bitmap[inode_num];
}

int check_b_bitmap(int *block_bitmap, int block_num){
    return block_bitmap[block_num];
}