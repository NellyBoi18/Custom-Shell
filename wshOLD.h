#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_ARGS 256
#define MAX_JOBS 256
#define MAX_LINE 1024

typedef struct {
    pid_t pid;
    int id;
    char* name;
    int is_background;
} job;

int num_jobs;
job jobs[MAX_JOBS];

void add_job(pid_t pid, char* name, int is_background);
void remove_job(int id);
void print_jobs();
void handle_signal(int signum);
void set_foreground(pid_t pid);
void set_background(pid_t pid);
void execute_command(char** args, int num_args, int is_background);
void execute_pipeline(char** args, int num_args, int is_background);
void execute_line(char* line, int is_batch);
void run_shell(int is_batch);