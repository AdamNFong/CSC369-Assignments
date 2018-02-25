#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>


struct entry_node {
    struct entry_node *next;
    char entry_name[256];
};

int main(int argc, char **argv) {

	char *path = argv[1];

    char *path_entry = strtok (path, "/");
    struct entry_node *head = NULL;
    struct entry_node *prev = head;
    /*construct linked list of path names for convenience*/
    while (path_entry != NULL){
    	struct entry_node new_node;
        strcpy (new_node.entry_name, path_entry);
    	new_node.next = NULL;
    	if (head == NULL){
    		head = &new_node;
    		prev = head;
    	}else{
			prev->next = &new_node;
			prev = &new_node;
		}
        path_entry = strtok(NULL, "/");
    	
    }
     struct entry_node *curr = head;
    while (curr != NULL){
        printf("%s\n", curr->entry_name);
        curr = curr->next;
    }
    return 0;
}