#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

// Struct to store background jobs
typedef struct { 
    pid_t pid;
    int id;
    char* name;
    int is_background;
} job;

int num_jobs = 0;
job jobs[256];

// Add a job to list of background jobs
void add_job(pid_t pid, char* name, int isBG) { 
    jobs[num_jobs].pid = pid; // Set process ID of job
    jobs[num_jobs].id = num_jobs + 1; // Set job ID
    jobs[num_jobs].name = strdup(name); // Set name/cmd of job
    jobs[num_jobs].is_background = isBG; // Set whether job is a background job
    num_jobs++;
}

// Remove a job from list of background jobs
void remove_job(int id) { 
    int i;

    // Iterate through list of jobs to find job with the given ID
    for (i = 0; i < num_jobs; i++) {
        if (jobs[i].id == id) {
            free(jobs[i].name); // Free the name of the job
            // Shift all jobs after the removed job to the left
            while (i < num_jobs - 1) {
                jobs[i] = jobs[i + 1];
                i++;
            }
            num_jobs--;
            break;
        }
    }
}

// Print list of background jobs
void print_jobs() { 
    int i;

    // Iterate through list of jobs and print each job
    for (i = 0; i < num_jobs; i++) {
        printf("%d: %s", jobs[i].id, jobs[i].name);
        // If background job
        if (jobs[i].is_background) {
            printf(" &");
        }
        printf("\n");
    }
}

// Handle signals
void handle_signal(int signum) { 
    if (signum == SIGINT) { // SIGINT signal
        pid_t pid = tcgetpgrp(STDIN_FILENO); // Get foreground process group
        if (pid > 0) { // If foreground process group exists
            kill(-pid, SIGINT); // Send SIGINT signal to process group to terminate the process
        }
    } else if (signum == SIGTSTP) { // SIGTSTP signal
        pid_t pid = tcgetpgrp(STDIN_FILENO); // Get foreground process group
        if (pid > 0) { // If foreground process group exists
            kill(-pid, SIGTSTP); // Send SIGTSTP signal to process group to stop the process
        }
    }
}

// Set given process to foreground
void set_foreground(pid_t pid) { 
    tcsetpgrp(STDIN_FILENO, pid); // Set foreground process group to given process
    signal(SIGINT, SIG_DFL); // Set signal handler for SIGINT to default
    signal(SIGTSTP, SIG_DFL); // Set signal handler for SIGTSTP to default
    kill(-pid, SIGCONT); // Send SIGCONT signal to process group to continue the process
    int status;
    waitpid(pid, &status, WUNTRACED); // Wait for process to finish or be stopped
    signal(SIGINT, handle_signal); // Set signal handler for SIGINT back to handle_signal
    signal(SIGTSTP, handle_signal); // Set signal handler for SIGTSTP back to handle_signal
    tcsetpgrp(STDIN_FILENO, getpid()); // Set foreground process group back to shell
}

// Set given process to background
void set_background(pid_t pid) {
    add_job(pid, "background", 1); // Add process to list of background jobs
    // Set given process to background using pid
    tcsetpgrp(STDIN_FILENO, getpid()); // Set foreground process group back to shell
    kill(-pid, SIGCONT); // Send SIGCONT signal to process group to continue the process
}

// Parse the input line into separate commands
void parseCmds(char *line, char ***commands, int *num_commands) {
    char *token;
    *num_commands = 0;
    token = strtok(line, ";");

    // While still cmds to parse
    while (token != NULL) { // 
        (*commands)[*num_commands] = token; // Add cmd to list of cmds
        (*num_commands)++; // Increment number of cmds
        token = strtok(NULL, ";"); // Get next cmd
    }
}
 
// Split line into arguments
int sepArgs(char *line, char ***args, int *num_args) {
    *num_args = 0;

    // Count num args
    char *l = strdup(line); // Copy line
    if(l == NULL){
        return -1;
    }

    char *token = strtok(l, " \t\n"); // Get first arg
    while(token != NULL){
        (*num_args)++; // Increment num args
        token = strtok(NULL, " \t\n"); // Get next arg
    }
    free(l);

    // Split line into args
    *args = malloc(sizeof(char **) * (*num_args+1));
    *num_args = 0;
    token = strtok(line, " \t\n"); // Get next arg
    while(token != NULL){
        char *token_copy = strdup(token); // Copy arg
        if(token_copy == NULL){
            return -1;
        }

        (*args)[(*num_args)++] = token_copy; // Add arg to list of args
        token = strtok(NULL, " \t\n"); // Get next arg
    }

    return 0;
}

// Execute command
int execCMD(char **args, int num_args) {
    pid_t pid;
    // Set signal handlers for SIGINT and SIGTSTP to default
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    
    pid = fork(); // Fork

    if (pid < 0) { // Fork error
        perror("fork");
        exit(-1);
    } else if (pid == 0) { // Child process
        if (getpid() != getsid(0)) {
            // The shell is not a session leader
            setpgid(0,0); // Set process group ID to new process group
        }
        // setpgid(0,0); // Set process group ID to new process group

        // Execute command
        if (execvp(args[0], args) == -1) { // If execvp() returns, error
            perror("execvp");
            exit(-1);
        }
    } else { // Parent process
        // Check through the jobs struct to see if the process is a background job
        int isBG = 0;
        for (int i = 0; i < num_jobs; i++) {
            if (jobs[i].pid == pid) { // If the process is a background job
                isBG = 1; // Set isBG to true
                break;
            }
        }

        if (!isBG) { // If foreground
            int status;
            waitpid(pid, &status, 0); // Wait for child process to finish
            // If child process was stopped, add it to the list of background jobs
            if (WIFSTOPPED(status)) { 
                add_job(pid, args[0], 1);
            }
        } else { // Background job
            // Add the job to the list of background jobs
            add_job(pid, args[0], 1);
            
            if (getpid() != getsid(0)) {
                // The shell is not a session leader
                setpgid(pid, jobs[num_jobs - 1].id); // Set the process group ID of the child process to the job ID
            }
            // setpgid(pid, jobs[num_jobs - 1].id);
        }
        // waitpid(pid, &status, 0);
    }

    return 0;
}

// Execute cmd. First checks for built-in cmds. If not, passes to execCMD()
int execute(char **args, int num_args) {
    // Built-in Commands
    if(strcmp(args[0], "cd") == 0) { // cd built-in command
        // Checking num args
        if (num_args != 2) {
            printf("cd: invalid arguments\n");
            return -1;
        }
        if (chdir(args[1]) == -1) { // Using chdir() with passed arg. If return, error
            perror("chdir");
            return -1;
        }

        return 0;
    } else if (strcmp(args[0], "jobs") == 0) { // jobs built-in command
        if (num_args > 1) {
            printf("jobs: too many arguments\n");
            return -1;
        }
        print_jobs();

        return 0;
    } else if (strcmp(args[0], "fg") == 0) { // fg built-in command
        if (num_args > 2) {
            perror("fg: too many arguments\n");
            return -1;
        }
        int id = num_jobs;
        if (num_args == 2) {
            id = atoi(args[1]); // Parse job ID to int

            // If job ID is invalid, error
            if (id <= 0 || id > num_jobs) { 
                perror("fg: invalid job id\n");
                return -1;
            }
        }
        job j = jobs[id - 1]; // Get job with given ID
        remove_job(id); // Remove job from list of background jobs
        set_foreground(j.pid); // Set job to foreground

        return 0;
    } else if (strcmp(args[0], "bg") == 0) { // bg built-in command
        if (num_args > 2) {
            perror("bg: too many arguments\n");
            return -1;
        }

        int id = num_jobs; // Default to last job
        if (num_args == 2) {
            id = atoi(args[1]); // Parse job ID to int

            // If job ID is invalid, error
            if (id <= 0 || id > num_jobs) { 
                perror("bg: invalid job id\n");
                return -1;
            }
        }
        job j = jobs[id - 1]; // Get job with given ID
        // remove_job(id); 
        set_background(j.pid); // Set job to background
        
        return 0;
    } 

    // Other exec program
    int a = execCMD(args, num_args);
    return a;
    
    return 0;
}

// Execute pipe
int execPipe(char **args, int num_args, char **pipedArgs, int numPipedArgs) {
    int pipefd[2]; // pipefd[0] is read end, pipefd[1] is write end
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(-1);
    }

    //printf("%d, and %d", pipefd[0], pipefd[1]);

    int pid = fork();
    if (pid == 0){ // Child
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]); // Close write end
        close(pipefd[0]); // Close read end
        execute(args, num_args); // Execute first command

        exit(0);
    } else { // Parent
        pid = fork(); // Fork again because we need two processes

        if(pid == 0) { // Child
            dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to pipe
            close(pipefd[0]); // Close read end
            close(pipefd[1]); // Close write end
            execute(pipedArgs, numPipedArgs); // Execute second command

            exit(0);
        } else { // parent
            int status; 
            close(pipefd[1]); // Close write end
            close(pipefd[0]); // Close read end
            waitpid(pid, &status, 0); // Wait for child process to finish
        } 
    }

    return 0;
}

int main() {
    // Set signal handlers for SIGINT and SIGTSTP to handle_signal
    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);

    char **args;
    char **commands = (char **)malloc(256 * sizeof(char *)); // List of commands
    int num_args, num_commands;
    char *buffer;
    size_t bufsize = 256;
    int hasPipe = 0;
    int pipeInd = -1;

    setbuf(stdout, NULL); // Disable buffering for stdout

    buffer = (char *)malloc(bufsize * sizeof(char));
    if(buffer == NULL)
    {
        perror("buffer");
        exit(-1);
    }

    // Main loop
    while(1) {
        printf("wsh> ");

        // Read the input line from the user
        if (getline(&buffer, &bufsize, stdin) != -1) {
            
            // Parse the input line into separate commands
            parseCmds(buffer, &commands, &num_commands);

            // Execute each command
            for (int i = 0; i < num_commands; i++) {
                hasPipe = 0;
                // Split command line into arguments
                sepArgs(commands[i], &args, &num_args);

                // Skip if no args
                if(num_args == 0) {
                    continue;
                }
                args[num_args] = NULL;

                // Handle exit as built in command
                if (strcmp(args[0], "exit") == 0) {
                    exit(0);
                }

                // Check for pipe
                for (int j = 0; j < num_args; j++) {
                    if (strcmp(args[j], "|") == 0) {
                        hasPipe = 1;
                        pipeInd = j;
                        break;
                    }
                }
                // Pipe
                if (hasPipe == 1) {
                    int numPrePipedArgs = pipeInd; // Num args before pipe
                    char *prePipedArgs[numPrePipedArgs + 1]; // List of args before pipe
                    // Add args before pipe to list
                    for (int j = 0; j < numPrePipedArgs; j++) { 
                        prePipedArgs[j] = args[j];
                    }
                    prePipedArgs[numPrePipedArgs] = NULL; // Set last arg to NULL
                    int numPipedArgs = (num_args - pipeInd) - 1; // Numargs after pipe
                    char *pipedArgs[numPipedArgs + 1]; // List of args after pipe
                    // Add args after pipe to list
                    for (int j = 0; j < numPipedArgs; j++) {
                        pipedArgs[j] = args[j + pipeInd + 1];
                    }
                    pipedArgs[numPipedArgs] = NULL; // Set last arg to NULL
                    args[pipeInd] = NULL; // Set pipe arg to NULL
                    num_args = pipeInd; // Set num args to num args before pipe

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

        