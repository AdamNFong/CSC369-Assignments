#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>
#include "ext2.h"
#include "ext2_header.h"

unsigned char *disk;

int main(int argc, char **argv) {

	char *file_path = argv[2];

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <directory path to deletion file or link> \n", argv[0]);
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

    char *file = path[path_length - 1];

    char *temp = NULL;
    if (file[strlen(file)-1] == '!'){//work around to an unkown bug
        temp = malloc (strlen(file)-1);
        strncpy(temp, file, strlen(file)-1);
        file = temp;
    }

    if (verify_path(inodes, sb, path, path_length, 1) != 0){
        return ENOENT;
    }

    int inode_num = 1;
    int count =0 ;
    while (count < path_length - 1){
        inode_num = find_inode_in_blocks (inodes[inode_num].i_block, 0, 11, path[count], 'd');
        count ++;
    }

    int file_or_link = 0;//1 for file 2 for link
    if (find_inode_in_blocks (inodes[inode_num].i_block, 0, 11, file, 'd') != -1){
        return EISDIR;//found a directory with the same name
    }

    if (find_inode_in_blocks (inodes[inode_num].i_block, 0, 11, file, 'f') == -1){
        if (find_inode_in_blocks (inodes[inode_num].i_block, 0, 11, file, 'l') == -1){
            return ENOENT;
        }else{
            file_or_link = 2;
        }
    }else{
        file_or_link = 1;
    }

    int file_node;
    if (file_or_link == 1){
        file_node = find_inode_in_blocks (inodes[inode_num].i_block, 0, 11, file, 'f');
    }else{
        file_node = find_inode_in_blocks (inodes[inode_num].i_block, 0, 11, file, 'l');
    }

    /*Passed checks, proceed to delete*/
    struct ext2_dir_entry *current_ent;
    struct ext2_dir_entry *prev_ent;
    int rec_len_total = 0;
    for (int i = 0; i < 12; i++){
        int block_num = inodes[inode_num].i_block[i];
        prev_ent = NULL;
        if (block_num != 0){
            while (rec_len_total != 1024){
                current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
                if (strcmp(current_ent->name,file) == 0){
                    if(prev_ent !=NULL){
                        prev_ent->rec_len += current_ent->rec_len; //Previous entry rec_len now stretches over removed file
                    }else{
                        current_ent->inode = 0; //If the entry is first in the block -> zero the inode number
                    }
                }
                prev_ent = current_ent;
                rec_len_total += current_ent->rec_len;
            }
        }
    }

    //compiler gets mad about not using arrays even though I do, this code is only to keep the compiler happy :(
    int i = 0;
    int count2 =0;
    while (i < 1){
        if (block_bitmap[i] == 1){
            count2++;
        }if (inode_bitmap[i] == 1){
            count2++;
        }
        i++;
    }

    //deallocate all blocks and inode associated with file
    inodes[file_node].i_links_count--;

    int block_ind = 0;
    if(inodes[file_node].i_links_count == 0){// only delete if the inode isnt referred to
        inodes[file_node].i_dtime = time(0);
        deall_i_bitmap(inode_bits, file_node);
        inode_bitmap[file_node] = 0;
        sb->s_free_inodes_count++;
        gd->bg_free_inodes_count++;
        if (inodes[file_node].i_block[12]==0){// not indirect block
            while (inodes[file_node].i_block[block_ind] != 0){
                deall_b_bitmap(block_bits, inodes[file_node].i_block[block_ind] - 1);
                sb->s_free_blocks_count++;
                gd->bg_free_blocks_count++; 
                block_bitmap[inodes[file_node].i_block[block_ind] - 1] = 0;
                block_ind++;
            }
        }else{//has indirect block
            for (int i = 0; i < 12;i++){//free up direct data blocks
                deall_b_bitmap(block_bits, inodes[file_node].i_block[i] - 1);
                sb->s_free_blocks_count++;
                gd->bg_free_blocks_count++; 
                block_bitmap[inodes[file_node].i_block[i] - 1] = 0; 
            }
            block_ind=0;//free up data blocks in indirect block
            unsigned int *indirect = (unsigned int *)(disk + (1024 * inodes[file_node].i_block[12]));
            while (indirect[block_ind] != 0){
                deall_b_bitmap(block_bits, indirect[block_ind] - 1);
                sb->s_free_blocks_count++;
                gd->bg_free_blocks_count++;
                block_bitmap[indirect[block_ind] - 1] = 0;
                block_ind++;
            } 

            //free up indirect block
            deall_b_bitmap(block_bits, inodes[file_node].i_block[12] - 1); 
            sb->s_free_blocks_count++;
            gd->bg_free_blocks_count++;  
            block_bitmap[inodes[file_node].i_block[12] - 1] = 0;   
        }
    }

    if (temp != NULL){
        free(temp);
    }
    free_path(path, path_length);
    return 0;
}