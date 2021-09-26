#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <getopt.h>

#include "main.h"

const char *GUI_PATH = "/Users/hp/Codes/FlintZilla/build/gui";
const char *CLI_PATH = "/Users/hp/Codes/FlintZilla/build/cli";

void print_cwd()
{
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("Current working dir: %s\n", cwd);
}

void check_root_access()
{
    if (setuid(0) == -1)
    {
        printf("Can't run the program!\nNo root access....\n");
        exit(-1);
    }
}

int main(int argc, char **argv)
{
    printf("Checking Arguments...\n");

    print_cwd();
    printf("Argument Requested: %s and %s\n\n", argv[1], argv[2]);

    int cli_flag = 0;
    int gui_flag = 0;
    int c;

    opterr = 0;

    while ((c = getopt(argc, argv, "cg")) != -1)
        switch (c)
        {
        case 'c':
            cli_flag = 1;
            break;
        case 'g':
            gui_flag = 1;
            break;
        case '?':
            fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            return 1;
        default:
            abort();
        }

    check_root_access();
    if (!is_bookmark_file_exists())
    {
        create_bookmark_file();
    }

    int pid = fork();
    if (pid == 0)
    {
        if (cli_flag)
        {
            printf("Running CLI...\n");

            static char *argv[] = {};
            execvp(CLI_PATH, argv);
            exit(127); /* only if execv fails */
        }

        if (gui_flag)
        {
            printf("Running GUI...\n");

            static char *argv[] = {};
            execvp(GUI_PATH, argv);
            exit(127); /* only if execv fails */
        }
    }
    /* pid!=0; parent process */
    else
    {
        wait(5);
        waitpid(pid, 0, 0); /* wait for child to exit */
    }

    return 0;
}