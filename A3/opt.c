#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"
#include "sim.h" //Global variable for trace file name located here

#define MAXLINE 256

struct node {
	addr_t addr;
	struct node *next;
};

extern int debug;

extern struct frame *coremap;

struct node *curr_node;

char temp[MAXLINE];


/* Page to evict is chosen using the optimal (aka MIN) algorithm. 
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {
	struct node *temp_curr;
	unsigned long dist = 0;
	int highest_dist = 0;
	int victim_page = 0;

	//calculate all necessary distances to first future occurrence for coremap entries
	for (int core_ind=0; core_ind < memsize; core_ind ++){
		temp_curr = curr_node;
		dist = 0;

		//0 signifies that this coremap entry needs its distance calculated
		if (coremap[core_ind].fst_occ == 0){
			while (temp_curr->addr != coremap[core_ind].vaddr >> PAGE_SHIFT){
				dist++;
				temp_curr = temp_curr->next;
				if (temp_curr == NULL){ //reached the end -> best victim
					coremap[core_ind].fst_occ = 0;
					return core_ind;
				}
			}
			coremap[core_ind].fst_occ = dist;//found future occurrence -> record distance
		}
	}

	//find max -> all entries reoccurr in the future
	highest_dist = 0;
	for (int core_ind = 0; core_ind < memsize; core_ind ++){
		if (coremap[core_ind].fst_occ > highest_dist){
			highest_dist = coremap[core_ind].fst_occ;
			victim_page = core_ind;
		}
	}

	coremap[victim_page].fst_occ = 0;
	return victim_page;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t *p) {
	struct node *prev_node;
	prev_node = curr_node;
	curr_node = curr_node->next;

	//if you weren't evicted, or the curr_node pointer hasn't passed the first occurence -> only need to decrement the distance of the coremap entries
	for (int i = 0; i< memsize; i++){
		if (coremap[i].fst_occ != 0){
			coremap[i].fst_occ--;
		}
	}

	free(prev_node);
	return;
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */ 
void opt_init() {
	FILE *tf_pointer;
	addr_t addr;
	struct node* prev_node = NULL;
	char type;//don't care

	if(tracefile != NULL) {
		if((tf_pointer = fopen(tracefile, "r")) == NULL) {
			perror("ERROR: opening tracefile:");
			exit(1);
		}
	}

	for (int i =0; i<memsize; i++){
		coremap[i].fst_occ = 0;
	}

	//create linked list, each node will have the vaddr >>PAGE_SHIFT to later compare
	while (fgets(temp, MAXLINE, tf_pointer) != NULL){
		struct node * np = malloc (sizeof(struct node));

		sscanf (temp, "%c %lx", &type, &addr);

		np->addr = addr >> PAGE_SHIFT;
		np->next=NULL;

		if (prev_node != NULL){
			prev_node ->next = np;
		}else{
			curr_node = np;
		}
		prev_node = np;

	}
	fclose(tf_pointer);

}