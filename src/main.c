#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "main.h"


int main(int argc, char **argv) {
    if(setuid(0)==-1){
        printf("Can't run the program!\nNo root access....\n");
        exit(-1);
    }

    if(!is_bookmark_file_exists()){
        create_bookmark_file();
    }
    printf("%d\n", is_bookmark_file_exists());
    return 0;
}