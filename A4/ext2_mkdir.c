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

	char *path = argv[2];

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <directory path>\n", argv[0]);
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

    /*check to see if there is even an inode available to use*/
    int new_inode_num = next_available_inode(inode_bitmap);
    if (new_inode_num == -1){
        return ENOMEM;//no memory left :(
    }

    /*check to see if there is a block available for the initial . and .. dirs*/
    int new_block_num = next_available_block(block_bitmap);
    if (new_block_num == -1){
        return ENOMEM;// no memory left
    }

    if (path[0] != '/'){
    	if (!(path[0] == '.' && path[1] =='/')){
    		return ENOENT;
    	}
    }

    //Tokens are inconvenient, so this will make an char **array for the path
    int path_length = get_path_length(path);
    char** trav_path = make_path_array (path, path_length);
    
    /* We must first verify directory path
		recieve 1 if the the path is invalid 
		recieve 0 if path is valid.

		return ENOENT or EEXIST if recieve 1
	 */
    if (path_length > 1){//making dir in the root node
        if (verify_path (inodes, sb, trav_path, path_length, 1) != 0){
        	return ENOENT;
        }
    }

    /*Make the directory: at this point the path is valid
    	Update inode bitmap
    	Add ext2_dir_entry to previous directory
    */
	inode_bitmap[new_inode_num] = 1;
    update_i_bitmap (inode_bits, new_inode_num);
    new_block_num = allocate_block(block_bitmap, block_bits);
    block_bitmap[new_block_num] = 1;
    sb->s_free_inodes_count -=1;
    sb->s_free_blocks_count -=1;
    gd->bg_free_inodes_count -=1;
    gd->bg_free_blocks_count -=1;


    int name_length = strlen (trav_path[path_length-1]);
 
    //new directory entry struct
    struct ext2_dir_entry new_entry;
    init_dir_entry(&new_entry, new_inode_num, name_length, EXT2_FT_DIR);
    int new_rec_len = calc_rec_len_div4(&new_entry);

    /*. dir*/
    struct ext2_dir_entry *dot_dir = (struct ext2_dir_entry *)(disk + (1024 * (new_block_num+1)));
    init_dir_entry (dot_dir, new_inode_num, 1, EXT2_FT_DIR);
    strncpy (dot_dir->name, ".", 1);
    dot_dir->rec_len = 12;

    //new inode struct
    struct ext2_inode new_inode;
    init_inode (&new_inode, EXT2_S_IFDIR);
    new_inode.i_block[0] = new_block_num + 1;//block numbering starts 1
    inodes[new_inode_num] = new_inode;


    int curr_inode_num = 1;
    int writable_block;

    if (path_length == 1){
    	struct ext2_dir_entry *double_dot_dir = (struct ext2_dir_entry *) (disk + (1024 * (new_block_num+1)) + 12);
    	init_dir_entry (double_dot_dir, 1, 2, EXT2_FT_DIR);
   		strncpy (double_dot_dir->name, "..", 2);
   		double_dot_dir->rec_len = 1012;
        inodes[1].i_links_count ++;
        writable_block = get_writable_block(inodes[1], inodes[1].i_block, new_rec_len, 0, block_bitmap, block_bits, 12, gd, sb);
    }else{
    	int ind = 0;
    	while (ind < path_length - 1){
    		curr_inode_num = find_inode_in_blocks(inodes[curr_inode_num].i_block, 0, 12, trav_path[ind],'d');
    		ind++;
    	}
    	//curr_inode_num will hold the previous inode
    	//e.g if we have path /foo/bar curr_inode_num will have the inode for foo
 
    	/*.. dir*/
    	struct ext2_dir_entry *double_dot_dir = (struct ext2_dir_entry *) (disk + (1024 * (new_block_num+1)) + 12);
    	init_dir_entry (double_dot_dir, curr_inode_num, 2, EXT2_FT_DIR);
    	strncpy (double_dot_dir->name, "..", 2);
   		double_dot_dir->rec_len = 1012;
        inodes[curr_inode_num].i_links_count ++;     
        writable_block = get_writable_block(inodes[curr_inode_num],inodes[curr_inode_num].i_block, new_rec_len, 0, block_bitmap, block_bits, 12, gd, sb);
    }

    /*Ran out of blocks*/
    if (writable_block == -1){
        	return ENOMEM;
        }

    /*Finally write to the appropriate block*/
    write_ent_to_block(writable_block + 1, new_inode_num + 1, name_length, EXT2_FT_DIR, trav_path[path_length-1]);

    free_path(trav_path, path_length);

/*If we reach this point we have succussfully made the directory without any errors*/
return 0;

}



