#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

int cur_page;

/* Page to evict is chosen using the fifo algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int fifo_evict() {
	//current page is the victim (coremap[0] is at the head then coremap[1] and so on)
	int victim_page = 0;
	//update current_page
	cur_page = (cur_page + 1) % memsize; //loop back around if reach the end of queue
	if (cur_page - 1 < 0){
		victim_page = memsize - 1;
	}else{
		victim_page = cur_page - 1;
	}
	return victim_page;
}

/* This function is called on each access to a page to update any information
 * needed by the fifo algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void fifo_ref(pgtbl_entry_t *p) {
//Do nothing, FIFO requires no reference updates
	return;
}

/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void fifo_init() {
	cur_page = 0;
}
