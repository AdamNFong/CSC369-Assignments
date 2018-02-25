#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void main (int argc, char**argv){

	int i =10;
	int k=9;
	if (k ==0){
	printf("%d, %d\n", (k+1) % i, 9);
}else{
	printf("%d, %d\n", (k+1) % i, k-1);
}
}