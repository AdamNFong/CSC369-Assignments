#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

int clock_hand;

/* Page to evict is chosen using the clock algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int clock_evict() {
	int victim_page = 0;
	pgtbl_entry_t *cur_page = coremap[clock_hand].pte; 
	while (cur_page->frame & PG_REF){//while page is referenced
		cur_page->frame = cur_page->frame & ~PG_REF;//set pg_ref bit to 0
		clock_hand = (clock_hand + 1) % memsize;//move clock hand
		cur_page = coremap[clock_hand].pte; 
	}

	victim_page = cur_page->frame >> PAGE_SHIFT;//only want page number
	clock_hand = (clock_hand + 1) % memsize;

	return victim_page;
}

/* This function is called on each access to a page to update any information
 * needed by the clock algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(pgtbl_entry_t *p) {
	p->frame = p->frame | PG_REF;
	return;
}

/* Initialize any data structures needed for this replacement
 * algorithm. 
 */
void clock_init() {
	clock_hand = 0;
}
