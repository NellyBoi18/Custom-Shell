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

int num_jobs = 0;
job jobs[256];

void add_job(pid_t pid, char* name, int is_background) {
    jobs[num_jobs].pid = pid;
    jobs[num_jobs].id = num_jobs + 1;
    jobs[num_jobs].name = strdup(name);
    jobs[num_jobs].is_background = is_background;
    num_jobs++;
}

void remove_job(int id) {
    int i;
    for (i = 0; i < num_jobs; i++) {
        if (jobs[i].id == id) {
            free(jobs[i].name);
            while (i < num_jobs - 1) {
                jobs[i] = jobs[i + 1];
                i++;
            }
            num_jobs--;
            break;
        }
    }
}

void print_jobs() {
    int i;
    for (i = 0; i < num_jobs; i++) {
        printf("[%d] %s", jobs[i].id, jobs[i].name);
        if (jobs[i].is_background) {
            printf(" &");
        }
        printf("\n");
    }
}

void handle_signal(int signum) {
    if (signum == SIGINT) {
        printf("\n");
    }
}

void set_foreground(pid_t pid) {
    tcsetpgrp(STDIN_FILENO, pid);
    kill(-pid, SIGCONT);
    int status;
    waitpid(pid, &status, WUNTRACED);
    tcsetpgrp(STDIN_FILENO, getpid());
}

void set_background(pid_t pid) {
    add_job(pid, "background", 1);
}

void parseCmds(char *line, char ***commands, int *num_commands) {
    
    char *token;
    *num_commands = 0;
    token = strtok(line, ";");
    while (token != NULL) {
        (*commands)[*num_commands] = token;
        (*num_commands)++;
        token = strtok(NULL, ";");
    }
}

int sepArgs(char *line, char ***args, int *num_args) {
    *num_args = 0;
    // count number of args
    char *l = strdup(line);
    if(l == NULL){
        return -1;
    }
    char *token = strtok(l, " \t\n");
    while(token != NULL){
        (*num_args)++;
        token = strtok(NULL, " \t\n");
    }
    free(l);
    // split line into args
    *args = malloc(sizeof(char **) * (*num_args+1));
    *num_args = 0;
    token = strtok(line, " \t\n");
    while(token != NULL){
        char *token_copy = strdup(token);
        if(token_copy == NULL){
            return -1;
        }
        (*args)[(*num_args)++] = token_copy;
        token = strtok(NULL, " \t\n");
    }
    return 0;
}

int execCMD(char **args, int num_args) {
    pid_t pid;
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    
    pid = fork();

    if (pid < 0) { // Fork error
        perror("fork");
        exit(-1);
    }
    else if (pid == 0) { // Child process
        setpgid(0,0); // Set process group ID to new process group
        if (execvp(args[0], args) == -1) {
            perror("execvp");
            exit(-1);
        }
    } 
    else {
        // Parent process
        // Check through the jobs struct to see if the process is a background job
        int is_background = 0;
        for (int i = 0; i < num_jobs; i++) {
            if (jobs[i].pid == pid) {
                is_background = 1;
                break;
            }
        }
        if (!is_background) {
            int status;
            waitpid(pid, &status, 0);
            // If child process was stopped, add it to the list of background jobs
            if (WIFSTOPPED(status)) { 
                add_job(pid, args[0], 1);
            }
        } else {
            // Add the job to the list of background jobs
            add_job(pid, args[0], 1);
            // Set the process group ID of the child process to the job ID
            setpgid(pid, jobs[num_jobs - 1].id);
        }
        // waitpid(pid, &status, 0);
    }

    return 0;
}

int execute(char **args, int num_args) {
    // Built-in Commands
    if(strcmp(args[0], "cd") == 0) {
        if (num_args != 2) {
            printf("cd: invalid arguments\n");
            return -1;
        }
        if (chdir(args[1]) == -1) {
            perror("chdir");
            return -1;
        }

        return 0;
    } else if (strcmp(args[0], "jobs") == 0) {
        if (num_args > 1) {
            printf("jobs: too many arguments\n");
            return -1;
        }
        print_jobs();

        return 0;
    } else if (strcmp(args[0], "fg") == 0) { // fg built-in command
        if (num_args > 2) {
            printf("fg: too many arguments\n");
            return -1;
        }
        int id = num_jobs;
        if (num_args == 2) {
            id = atoi(args[1]);
            if (id <= 0 || id > num_jobs) {
                printf("fg: invalid job id\n");
                return -1;
            }
        }
        job j = jobs[id - 1];
        remove_job(id);
        set_foreground(j.pid);
        return 0;
    } else if (strcmp(args[0], "bg") == 0) { // bg built-in command
        if (num_args > 2) {
            printf("bg: too many arguments\n");
            return -1;
        }
        int id = num_jobs;
        if (num_args == 2) {
            id = atoi(args[1]);
            if (id <= 0 || id > num_jobs) {
                printf("bg: invalid job id\n");
                return -1;
            }
        }
        job j = jobs[id - 1];
        remove_job(id);
        set_background(j.pid);
        
        return 0;
    } 

    // Other exec program
    int a = execCMD(args, num_args);
    return a;
    
    return 0;
}

int execPipe(char **args, int num_args, char **pipedArgs, int numPipedArgs) {
    int pipefd[2];
    // pipe(pipefd);
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(-1);
    }

    //printf("%d, and %d", pipefd[0], pipefd[1]);

    int pid = fork();
    if (pid == 0){ // child

        dup2(pipefd[1], STDOUT_FILENO);

        close(pipefd[1]);
        close(pipefd[0]);
        // execute cat
        execute(args, num_args);
        exit(1);
    }
    else{
        pid=fork();

        if(pid==0) { // child
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
            execute(pipedArgs, numPipedArgs);
            exit(1);
        } else { // parent
            int status;
            close(pipefd[1]);
            close(pipefd[0]);
            waitpid(pid, &status, 0);
        }

 
    }

    return 0;
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);

    char **args;
    char **commands = (char **)malloc(100 * sizeof(char *));
    int num_args, num_commands;
    char *buffer;
    size_t bufsize = 256;
    int hasPipe = 0;
    int pipeInd = -1;

    setbuf(stdout, NULL);

    buffer = (char *)malloc(bufsize * sizeof(char));
    if(buffer == NULL)
    {
        fprintf(stderr, "wsh: expected 0 or 1 argument\n");
        exit(-1);
    }


    while(1) {
        printf("wsh> ");

        // Read the input line from the user
        if (getline(&buffer, &bufsize, stdin) != -1) {
            
            // Parse the input line into separate commands
            parseCmds(buffer, &commands, &num_commands);

            // Execute each command
            for (int i = 0; i < num_commands; i++) {
                hasPipe = 0;
                // split command line into arguments
                sepArgs(commands[i], &args, &num_args);

                // skip if no args
                if(num_args == 0) {
                    continue;
                }
                args[num_args] = NULL;

                // exit if exit
                if (strcmp(args[0], "exit") == 0) {
                    exit(0);
                }

                //check for pipe
                for (int j = 0; j < num_args; j++) {
                    if (strcmp(args[j], "|") == 0) {
                        hasPipe = 1;
                        pipeInd = j;
                        break;
                    }
                }
                if (hasPipe == 1) {
                    int numPrePipedArgs = pipeInd;
                    char *prePipedArgs[numPrePipedArgs + 1];
                    for (int j = 0; j < numPrePipedArgs; j++) {
                        prePipedArgs[j] = args[j];
                    }
                    prePipedArgs[numPrePipedArgs] = NULL;
                    int numPipedArgs = (num_args - pipeInd) - 1;
                    char *pipedArgs[numPipedArgs + 1];
                    for (int j = 0; j < numPipedArgs; j++) {
                        pipedArgs[j] = args[j + pipeInd + 1];
                    }
                    pipedArgs[numPipedArgs] = NULL;
                    args[pipeInd] = NULL;
                    num_args = pipeInd;
                    execPipe(prePipedArgs, numPrePipedArgs, pipedArgs, numPipedArgs);
                    continue;
                }
                // Otherwise, execute the command as normal
                else {
                    execute(args, num_args);
                }
            }
        }        
        
    }
    free(commands);
    free(buffer);
    free(args);
    return 0;
}

        