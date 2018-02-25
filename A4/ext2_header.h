#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

void init_inode(struct ext2_inode *new_inode, unsigned short i_mode);
void init_dir_entry(struct ext2_dir_entry *new_entry, int new_inode_num, int name_length, unsigned char type);
int verify_path (struct ext2_inode* inodes, struct ext2_super_block *sb, char** path_head, int path_length, int include_last);
int next_available_inode (int *inode_bitmap);
int next_available_block (int *block_bitmap);
int calc_rec_len_div4 (struct ext2_dir_entry *entry);
int calc_rec_len_div4_name(int name_length);
void write_ent_to_block(int block_num, int inode, int name_len, unsigned short file_type, char* name);
int find_inode_in_blocks(unsigned int *i_block, int search_block_ind, int search_limit ,char *curr, char search_type);
int allocate_block (int *block_bitmap, unsigned char *block_bits);
int get_writable_block (struct ext2_inode inode, unsigned int *i_block, int n_rec_len, int search_block_ind, int *block_bitmap,unsigned char *block_bits, int search_limit, struct ext2_group_desc * gd, struct ext2_super_block *sb);
void update_b_bitmap(unsigned char *block_bits, int new_block_num);
void update_i_bitmap (unsigned char *inode_bits, int new_inode_num);
void deall_i_bitmap (unsigned char *inode_bits, int free_inode_num);
void deall_b_bitmap(unsigned char *block_bits, int free_block_num);
char dir_type (unsigned char type);
char f_type (unsigned short i_mode);
char **make_path_array(char *path, int path_length);
void separate_path(char *path, char **path_array);
int get_path_length(char *path);
void free_path(char **path_array, int path_length);