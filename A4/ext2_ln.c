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

//0 - name, 1 - imagefile, 2 - -s, 3 - file1, 4 - file2
int main (int argc, char **argv){
    int bool_symlink = 0;
    char *target_path;
    char *linkname_path;
    if (argc == 4){//hard link
	    target_path = argv[2];
	    linkname_path = argv[3];
    }else if (argc == 5){
        target_path = argv[3];
        linkname_path = argv[4];
        if (strcmp ("-s", argv[2]) == 0){
            bool_symlink = 1;
        }else{
            fprintf(stderr, "Usage: %s <image file name> <-s?> <directory path to file> <directory path to destination>\n", argv[0]);
            exit(1);
        }
    }else{
        fprintf(stderr, "Usage: %s <image file name> <-s?> <directory path to file> <directory path to destination>\n", argv[0]);
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

    int target_length = get_path_length(target_path);
    char **target = make_path_array (target_path, target_length);
    int link_path_length = get_path_length(linkname_path);
    char **linkpath = make_path_array (linkname_path, link_path_length);

    char* linkname = linkpath[link_path_length-1];

    /*If symlink check if an inode and a block are available*/
    if (bool_symlink){
    	if (next_available_inode(inode_bitmap) == -1 || next_available_block(block_bitmap) == -1){
    		return ENOMEM;
    	}
    }

    /*verify that paths are valid*/
    if (bool_symlink == 0){//symlinks can link to garabge paths no need verify the target path
    	if (verify_path(inodes, sb, target, target_length, 1) != 0){
        	return ENOENT;
    	}
	}

    if (verify_path(inodes, sb, linkpath, link_path_length, 1) != 0){
        return ENOENT;
    }

    /*Error checks to see if target files doesnt exist or if the link file already exists*/

    //target to link to
	int trav_count = 0;
	int trav_inode_num = 1;
	int target_inode_num = 0;
	if (bool_symlink == 0){//only need to traverse to target if it's a hard link
	    while (trav_count < target_length - 1){
	        trav_inode_num = find_inode_in_blocks(inodes[trav_inode_num].i_block, 0, 12, target[trav_count], 'd');
	        trav_count ++;
	    }
	    if (find_inode_in_blocks(inodes[trav_inode_num].i_block, 0, 12, target[trav_count], 'f') == -1){
	        return ENOENT;
	    }

	    target_inode_num =find_inode_in_blocks(inodes[trav_inode_num].i_block, 0, 12, target[trav_count], 'f');
	}
    //link to create
    int trav2_count = 0;
    int trav2_inode_num = 1;
    while (trav2_count < link_path_length - 1){
        trav2_inode_num = find_inode_in_blocks(inodes[trav2_inode_num].i_block, 0, 12, linkpath[trav2_count], 'd');
        trav2_count ++;
    }
    if (find_inode_in_blocks(inodes[trav2_inode_num].i_block, 0, 12, linkpath[trav2_count], 'f') != -1){
        return EEXIST;
    }

    //hardlink and the link file is really a directory
    if (bool_symlink == 0 && find_inode_in_blocks(inodes[trav_inode_num].i_block, 0, 12, target[trav_count], 'd') != -1){
        return EISDIR;
    }

    /*Passed initial checks, now begin linking*/
    int writable_block;
    writable_block = get_writable_block (inodes[trav2_inode_num], inodes[trav2_inode_num].i_block,strlen (linkname), 0, block_bitmap, block_bits, 11, gd, sb);
    
    int all_block = 0;
    if (bool_symlink){ //sym link
    	int new_inode_num = next_available_inode(inode_bitmap);
    	inode_bitmap[new_inode_num] = 1;
    	update_i_bitmap(inode_bits, new_inode_num);

    	write_ent_to_block (writable_block + 1, new_inode_num +1, strlen(linkname), EXT2_FT_SYMLINK, linkname);
    	//write target paht to file

    	struct ext2_inode new_inode;
    	init_inode (&new_inode, EXT2_S_IFLNK);
    	sb->s_free_inodes_count -=1;
    	gd->bg_free_inodes_count -=1;

        //set up symlink file
    	all_block = allocate_block(block_bitmap, block_bits);
        sb->s_free_blocks_count-=1;
        gd->bg_free_blocks_count-=1;
        block_bitmap[all_block] = 1;
    	new_inode.i_block[0]= all_block + 1;
    	inodes[new_inode_num] = new_inode;

        //write path
    	memcpy(disk + (1024 * (all_block + 1)), target_path, strlen(target_path));
    }else{ //hardlink
    	write_ent_to_block (writable_block + 1,target_inode_num +1, strlen(linkname), EXT2_FT_REG_FILE, linkname);
    	inodes[target_inode_num].i_links_count ++;
    }


    free_path (target, target_length);
    free_path (linkpath, link_path_length);

    return 0;//if we reach this point we succeeded :)
}