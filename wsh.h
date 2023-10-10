#ifndef WSH_H
#define WSH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

typedef struct {
    pid_t pid;
    int id;
    char* name;
    int is_background;
} job;

// Function declarations
void add_job(pid_t pid, char* name, int is_background);
void remove_job(int id);
void print_jobs();
void handle_signal(int signum);
void set_background(pid_t pid);
void parseCmds(char *line, char ***commands, int *num_commands);
int sepArgs(char *line, char ***args, int *num_args);
int execCMD(char **args, int num_args);
int execute(char **args, int num_args);
int execPipe(char **args, int num_args, char **pipedArgs, int numPipedArgs);

#endif
