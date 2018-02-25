#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;


int timestamp;


/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int lru_evict() {
	int i = 0;
	int victim_page = 0;
	int min_timestamp = timestamp;//all timestamps in coremap will be equal to or less then variable:timestamp

	//now search for the lowest time, the one with the lowest time is the least referenced page in coremap
	while (i < memsize){
		if (coremap[i].time < min_timestamp){
			min_timestamp = coremap[i].time;
			victim_page = i;
		}
		i++;
	}

	return victim_page;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t *p) {
	coremap[p->frame >> PAGE_SHIFT].time = timestamp;//stamp the frame with a time
	timestamp++;
	return;
}


/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void lru_init() {
	timestamp = 0;
	for (int i = 0; i < memsize; i++){
		coremap[i].time = 0;
	}
}
