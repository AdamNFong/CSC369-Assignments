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
    block_check (inode_bitmap, sb, gd, &total_fixes);   

    /*b.*/
    i_mode_check (inodes[2].i_block, 0, &total);

    /*c.*/
    inode_all_check(inodes[2].i_block, 0, &total, inode_bitmap, inode_bits, sb, gd);

    /*d.*/
    i_dtime_check(inodes[2].i_block, 0, &total);
return 0;
}

int abs_value (int value){
    if (value < 0){
        return -value;
    }else{
        return value;
    }
}

void inode_check (int *inode_bitmap, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *total){
    int temp_count = 0;
    int diff = 0;
    for (int i = 0; i < 32; i++){
        if (inode_bitmap[i] == 0){
            temp_count ++;
        }
    }
    if (temp_count != sb->s_free_inodes_count){
        diff = temp_count - sb->s_free_inodes_count;
        if (diff < 0){
            sb->s_free_inodes_count -=diff;
        }else{
             sb->s_free_inodes_count +=diff;
        }
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap", abs_value(diff)); 
        *total += abs_value(diff);
    }

    if (temp_count != gd->bg_free_inodes_count){
        diff = temp_count - gd->bg_free_inodes_count;
        if (diff < 0){
            gd->bg_free_inodes_count -=diff;
        }else{
            gd->bg_free_inodes_count +=diff;
        }
        *total += abs_value(diff);
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap", abs_value(diff)); 
    }
}

void block_check (int *inode_bitmap, struct ext2_super_block *sb, struct ext2_group_desc *gd, int *total){
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
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap", abs_value(diff)); 
    }

    if (temp_count != gd->bg_free_blocks_count){
        diff = temp_count - gd->bg_free_blocks_count;
        if (diff < 0){
            gd->bg_free_inodes_count -=diff;
        }else{
            gd->bg_free_inodes_count +=diff;
        }
        *total += abs_value(diff);
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap", abs_value(diff)); 
    }   
}

void i_mode_check(unsigned int* i_block, int ind, int *total){
    struct ext2_dir_entry *current_ent;
    int rec_len_total;

    if (i_block[ind] != 0){
        int block_num = i_block[ind] + 1;
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
            struct ext2_inode insp_inode = inodes [current_ent->inode - 1];
            if (strncmp("..",current->name,2) != 0 && strncmp(".",current->name,1) != 0){
                if (f_type(insp.i_mode) != dir_type(current_ent->file_type)){
                    current_ent->file_type = inode_to_dir_type (insp_inode.i_mode);
                    printf("Fixed: Entry type vs inode mismatch: inode [%d]", current_ent->inode);
                    *total++;
                }
                if (dir_type(current_ent) == 'd'){
                    i_mode_check(insp_inode.i_block, 0 ,total);
                }
            }
            rec_len_total += current_ent->rec_len;
        }
        i_mode_check(i_block, ind + 1, total);
    }else{
        return;
    }
}

void inode_all_check(unsigned int *i_block, int ind, int *total, int *inode_bitmap, unsigned char *inode_bits, struct ext2_super_block *sb, struct ext2_group_desc *gd){
    struct ext2_dir_entry *current_ent;
    int rec_len_total;

    if (i_block[ind] != 0){
        int block_num = i_block[ind] + 1;
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
            struct ext2_inode insp_inode = inodes [current_ent->inode - 1];
            if (strncmp("..",current->name,2) != 0 && strncmp(".",current->name,1) != 0){
                if (check_bitmap(inode_bitmap, current->inode -1) == 0){
                    update_i_bitmap(inode_bits,current->inode -1);
                    inode_bitmap[current->inode-1] = 1;
                    sb->s_free_inodes_count--;
                    gd->bg_free_inodes_count--;
                    printf("Fixed: inode [%d] not marked as in-use", current_ent->inode);
                    *total++;
                }
                if (dir_type(current_ent) == 'd'){
                    i_mode_check(insp_inode.i_block, 0 ,total,inode_bitmap ,inode_bits, sb, gd);
                }
            }
            rec_len_total += current_ent->rec_len;
        }
        inode_all_check(i_block, ind + 1, total);
    }else{
        return;
    }
}

void i_dtime_check(unsigned int* i_block, int ind, int *total){
    struct ext2_dir_entry *current_ent;
    int rec_len_total;

    if (i_block[ind] != 0){
        int block_num = i_block[ind] + 1;
        while (rec_len_total != 1024){
            current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
            struct ext2_inode insp_inode = inodes [current_ent->inode - 1];
            if (strncmp("..",current->name,2) != 0 && strncmp(".",current->name,1) != 0){
                if (insp_inode.i_dtime == 0){
                    insp_inode.i_dtime = 0;
                    printf("Fixed: valid inode marked for deletion: [%d]", current_ent->inode);
                    *total++;
                }
                if (dir_type(current_ent) == 'd'){
                    i_mode_check(insp_inode.i_block, 0 ,total);
                }
            }
            rec_len_total += current_ent->rec_len;
        }
        i_dtime_check(i_block, ind + 1, total);
    }else{
        return;
    }
}

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

int check_bitmap(int *inode_bitmap, int inode_num){
    return inode_bitmap[inode_num];
}