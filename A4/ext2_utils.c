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

extern unsigned char *disk;

/*inializes an inode*/
void init_inode(struct ext2_inode *new_inode, unsigned short i_mode){
    new_inode->i_mode = i_mode;
    new_inode->i_size = 1024;
    new_inode->i_uid = 0;
    new_inode->i_dtime = 0;
    new_inode->i_gid = 0;
    new_inode->i_links_count = 1;
    new_inode->i_blocks = 2; // initial block for . and .. dirs or whatever is stored in the file
    new_inode->osd1 = 0;
    for (int i = 0; i< 15; i++){
    	new_inode->i_block[i] = 0;
    }
    new_inode->i_generation = 0;
    new_inode->i_file_acl = 0;
    new_inode->i_dir_acl = 0;
    new_inode->i_faddr = 0;
}

/*Initializes a directory entry*/
void init_dir_entry(struct ext2_dir_entry *new_entry, int new_inode_num, int name_length, unsigned char type){
    new_entry->inode = new_inode_num + 1; //inodes numbering starts at 1
    new_entry->rec_len = 0;//dont know the rec len yet
    new_entry->name_len = name_length;
    new_entry->file_type = type;
}

/* Verifies directory path
	return 1 if the the path is invalid 
	return 0 if path is valid.
 */
int verify_path (struct ext2_inode* inodes, struct ext2_super_block *sb, char** path_head, int path_length, int include_last){

    unsigned int inode_num = 0;
    int index =0;
    while (index < path_length - include_last){
        
        //unable to a find an entry
        if (inode_num == -1){
            return 1;
        }

        if (index == 0){
            inode_num = find_inode_in_blocks(inodes[1].i_block, 0, 12, path_head[index], 'd'); //check root
        }else{
            inode_num= find_inode_in_blocks(inodes[inode_num].i_block, 0, 12, path_head[index], 'd');
        }
        index++;
    }
    return 0; //if the loop terimates the path is valid
}

/*returns (inode number-1) of next available inode*/
int next_available_inode(int *inode_bitmap){
    for (int i = 11; i < 32; i++){
        if (inode_bitmap[i] == 0){
            return i;
        }
    }
    return -1;
}

/*returns (block number-1) of next available block*/
int next_available_block(int *block_bitmap){
    for (int i = 0; i < 128; i++){
        if (block_bitmap[i] == 0){
            return i;
        }
    }
    return -1;
}

/*Calculates rec_len and makes sure is divisible by 4*/
int calc_rec_len_div4(struct ext2_dir_entry *entry){
    int rec_len = 8 + entry->name_len;
    
    while (rec_len % 4 != 0){
        rec_len++;
    }

    return rec_len; 
}

/*Calculates rec_len and makes sure is divisible by 4*/
int calc_rec_len_div4_name(int name_length){
    int rec_len = 8 + name_length;
    
    while (rec_len % 4 != 0){
        rec_len++;
    }

    return rec_len; 
}

/*writes a directory entry to a specific block
 Assumption: this block has space
 */
void write_ent_to_block(int block_num, int inode, int name_len, unsigned short file_type, char* name){
    struct ext2_dir_entry * current_ent;
    int rec_len_total = 0;
    struct ext2_dir_entry * dir_write;

    while (rec_len_total != 1024){
        current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
        if (current_ent->rec_len + rec_len_total == 1024){
            current_ent->rec_len = calc_rec_len_div4(current_ent);

            dir_write = (struct ext2_dir_entry *) (disk + (1024 * block_num) + (rec_len_total + current_ent->rec_len)); 

            dir_write->inode = inode;
            dir_write->rec_len = 1024 - (rec_len_total + current_ent->rec_len);
            dir_write->name_len = name_len;
            dir_write->file_type = file_type;
            strncpy (dir_write->name, name, name_len);
            return;
        }
        rec_len_total += current_ent -> rec_len;
    }
}

/*returns last block in inode that has space for a new entry, allocates block if necessary*/
int get_writable_block (struct ext2_inode inode, unsigned int *i_block, int n_rec_len, int search_block_ind, int *block_bitmap,unsigned char *block_bits, int search_limit, struct ext2_group_desc * gd, struct ext2_super_block *sb){
	int block_num = i_block[search_block_ind];
    int rec_len_total = 0;
	struct ext2_dir_entry *current_ent;

	if (i_block[search_block_ind + 1] == 0){//last block in use
		while (rec_len_total != 1024){
			current_ent = (struct ext2_dir_entry *)(disk + (1024 * block_num) + rec_len_total);
			if (current_ent->rec_len + rec_len_total == 1024){
				if (current_ent->rec_len >= n_rec_len){
					return block_num-1;
				}
			}
            rec_len_total += current_ent ->rec_len;
		}
		//allocate block
		if (search_block_ind < search_limit){
			int all_block = allocate_block(block_bitmap, block_bits);
			block_bitmap [all_block] = 1;
			gd->bg_free_blocks_count -=1;
            sb->s_free_blocks_count -=1;
			inode.i_blocks += 2;
            inode.i_size += 1024;
			if (all_block == -1){
				return -1;//no more blocks
			}else{
				i_block[search_block_ind + 1] = all_block;
				return all_block;
			}
		}else {//=11 use indirect block
			int indirect = allocate_block(block_bitmap, block_bits);
			int indirect_data = allocate_block(block_bitmap, block_bits);
			block_bitmap [indirect] = 1;
			block_bitmap [indirect_data] = 1;
			gd->bg_free_blocks_count -=2;
            sb->s_free_blocks_count -=2;
			inode.i_blocks += 4;
            inode.i_size += 2048;
			if (indirect == -1 || indirect_data == -1){//need 2 blocks: one for indirect and and indirect data
				return -1;//no more blocks
			}else{
				i_block[12] = indirect;
				unsigned int * indirect_block_array = (unsigned int *) (disk + 1024 * indirect);
				indirect_block_array[0] = indirect_data; 
			}
		}
	}else{//recurse
		if (search_block_ind < search_limit){
			return get_writable_block(inode, i_block, n_rec_len, search_block_ind + 1, block_bitmap, block_bits, search_limit, gd, sb);
		}else{
			unsigned int* indirect_i_block = (unsigned int*)disk + 1024 * i_block[12];//start traversing indirect block
			return get_writable_block(inode,indirect_i_block, n_rec_len, 0, block_bitmap, block_bits, 116, gd, sb);//use indirect_block_array set search_limit 116 since indirect block can hold more indirect pointers
		}
	}
    return -1;
}

/*Allocates a new block for usage */
int allocate_block (int *block_bitmap, unsigned char *block_bits){
	int block_num = next_available_block(block_bitmap);
	if (block_num == -1){
		return -1;//failure
	}
	update_b_bitmap (block_bits, block_num);
	return block_num;
}

/*Finds specific inode in directory entries of a particular inode. 
 Usage: traversing the filesystem*/
int find_inode_in_blocks(unsigned int *i_block, int search_block_ind, int search_limit ,char *curr, char search_type){
	struct ext2_dir_entry *current_ent;
	char* ent_name;
	int rec_len_total = 0;

	int block_num = i_block[search_block_ind];

	if (i_block[search_block_ind] != 0){
		while (rec_len_total != 1024){
			current_ent = (struct ext2_dir_entry *)(disk + 1024 * block_num + rec_len_total);
			ent_name = (char *)(disk + 1024 * block_num + sizeof(struct ext2_dir_entry) + rec_len_total);
			if (strncmp (ent_name, curr, strlen(curr)) == 0 && dir_type(current_ent->file_type)==search_type){
				return (current_ent->inode) - 1;
			}
			rec_len_total += current_ent -> rec_len;
		}
	}else{//recurse
		if (search_block_ind < search_limit){
			return find_inode_in_blocks(i_block, search_block_ind + 1, search_limit, curr, search_type);
		}else{
			unsigned int* indirect_i_block = (unsigned int*) (disk + (1024 * i_block[12]));//indirect_block containing array of 4-byte block ids
			return find_inode_in_blocks(indirect_i_block, 0, 200, curr, search_type);//use indirect_block_array set search_limit 116 since indirect block can hold more indirect pointers
		}
	}
	return -1;// base case, if you reach this it means you searched all available blocks in inode and didnt find the entry
}

/*Updates the inode bitmap, -1*/ 
void update_i_bitmap (unsigned char *inode_bits, int new_inode_num){
	int byte = new_inode_num / 8;
	int bit = new_inode_num % 8;
	inode_bits[byte] |= (1 << bit);
}

/*Updates the block bitmap*/
void update_b_bitmap(unsigned char *block_bits, int new_block_num){
	int byte = new_block_num / 8;
	int bit = new_block_num % 8;
	block_bits[byte] |= (1 << bit);
}

void deall_i_bitmap (unsigned char *inode_bits, int free_inode_num){
    int byte = free_inode_num / 8;
    int bit = free_inode_num % 8;
    inode_bits[byte] &= (~(1 << bit));
}

/*Updates the block bitmap*/
void deall_b_bitmap(unsigned char *block_bits, int free_block_num){
    int byte = free_block_num / 8;
    int bit = free_block_num % 8;
    block_bits[byte] &= (~(1 << bit));
}


/*returns the file type from directory entry struct field file_type*/
char dir_type (unsigned char type){
switch (type){
        case  EXT2_FT_UNKNOWN:
            return '0'; //Unkown file_type
        case EXT2_FT_REG_FILE:
            return 'f'; //file
        case EXT2_FT_DIR:
            return 'd'; //directory
        case EXT2_FT_SYMLINK:
            return 'l'; //link
        default:   
            return 'e';//error
    }
}

/*returns the file type from inode field i_mode*/
char f_type (unsigned short i_mode){
    switch ((i_mode>>12)<<12){
        case EXT2_S_IFLNK:
            return 'l'; //link
        case EXT2_S_IFREG:
            return 'f'; //file
        case EXT2_S_IFDIR:
            return 'd'; //directory
        default:   
            return 'e';//error
    }
}

/*returns a char** to a malloced array of strings*/
char **make_path_array(char *path, int path_length){
    char **path_array;
    path_array = malloc(sizeof(char *) * path_length);

    //get names of all tokens in the path
    separate_path(path, path_array);

    return path_array;
}

/*Returns the path length (number of directories)*/
int get_path_length(char *path){
    int count = 0;
    int path_string_ind = 0;
    if (strlen (path) > 1){
        while(path[path_string_ind] != '\0'){
            if(path[path_string_ind] == '/' && path[path_string_ind+1] != '/'){ // repeated slashes
                count++;
            }
            path_string_ind++;
        }
    }
    return count;
}

/*helper function to make_path_array: Uses string tokenizer to place names of directories into array*/
void separate_path(char *path, char **path_array){
    int path_string_ind = 0;
    char *temp_path = malloc(strlen(path));
    strcpy(temp_path, path);
    char *token = strtok(temp_path, "/");

    //use tokens and copy into the string array (char**)
    while(token != NULL){
        path_array[path_string_ind] = malloc(strlen(token));
        strcpy(path_array[path_string_ind], token);
        token = strtok(NULL, "/");

        path_string_ind++;
    }

    free(temp_path);
}

/* Frees the path array*/
void free_path(char **path_array, int path_length){
	for (int path_ind = 0 ; path_ind < path_length; path_ind++){
		free (path_array[path_ind]);
	}
	free(path_array);
};
