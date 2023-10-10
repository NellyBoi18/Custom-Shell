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

int num_jobs = 0;
job jobs[MAX_JOBS];

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

/*
void execute_command(char** args, int num_args, int is_background) {
    pid_t pid = fork(); // Fork process
    if (pid == 0) { // If child, execute command
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL); 
        execvp(args[0], args);
        // If execvp returns, there was an error
        perror("execvp");
        exit(-1);
    } else if (pid > 0) { // If parent, set foreground or background
        if (!is_background) {
            set_foreground(pid);
        } else {
            set_background(pid);
        }
    } else {
        perror("fork");
    }
}
*/

void execute_command(char** args, int num_args, int is_background) {
    pid_t pid = fork();
    if (pid == 0) { // If child, execute command
        // Set signal handlers
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        char* path = getenv("/bin"); // Get PATH environment variable
        char* dir = strtok(path, ":"); // Get first directory

        // Loop through directories in PATH
        while (dir != NULL) {
            char* cmd = malloc(strlen(dir) + strlen(args[0]) + 2);
            sprintf(cmd, "%s/%s", dir, args[0]); // Construct command
            // If command exists, execute it
            if (access(cmd, X_OK) == 0) {
                execvp(cmd, args);
                perror("execvp");
                exit(-1);
            }
            free(cmd);
            dir = strtok(NULL, ":"); // Get next directory
        }
        perror(args[0]);
        exit(-1);
    } else if (pid > 0) {
        if (!is_background) {
            set_foreground(pid);
        } else {
            set_background(pid);
        }
    } else {
        perror("fork");
    }
}

void execute_pipeline(char** args, int num_args, int is_background) {
    int i, j, k;
    int num_pipes = 0;
    int pipe_indices[MAX_ARGS];
    pipe_indices[num_pipes++] = -1;
    for (i = 0; i < num_args; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_indices[num_pipes++] = i;
        }
    }
    pipe_indices[num_pipes++] = num_args;
    int num_commands = num_pipes - 1;
    int pipefds[num_commands][2];
    for (i = 0; i < num_commands; i++) {
        if (pipe(pipefds[i]) == -1) {
            perror("pipe");
            return;
        }
    }
    for (i = 0; i < num_commands; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO);
            }
            if (i < num_commands - 1) {
                dup2(pipefds[i][1], STDOUT_FILENO);
            }
            for (j = 0; j < num_commands; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }
            int start = pipe_indices[i] + 1;
            int end = pipe_indices[i + 1];
            char* command[end - start + 1];
            for (j = start, k = 0; j < end; j++, k++) {
                command[k] = args[j];
            }
            command[k] = NULL;
            execvp(command[0], command);
            perror("execvp");
            exit(-1);
        } else if (pid > 0) {
            close(pipefds[i][1]);
            if (!is_background && i == num_commands - 1) {
                set_foreground(pid);
            } else {
                set_background(pid);
            }
        } else {
            perror("fork");
        }
    }
    for (i = 0; i < num_commands; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
}

void execute_line(char* line, int is_batch) {
    int num_args = 0;
    char* args[MAX_ARGS];
    char* token = strtok(line, " \n"); // Get first token

    // Parse line into arguments
    while (token != NULL) {
        args[num_args++] = token;
        token = strtok(NULL, " \n");
    }
    // If no arguments, return
    if (num_args == 0) {
        return;
    }

    if (strcmp(args[0], "exit") == 0) { // exit built-in command
        if (num_args > 1) {
            printf("exit: too many arguments\n");
            return;
        }
        exit(0);
    } else if (strcmp(args[0], "cd") == 0) { // cd built-in command
        if (num_args != 2) {
            printf("cd: invalid arguments\n");
            return;
        }
        if (chdir(args[1]) == -1) {
            perror("chdir");
        }
    } else if (strcmp(args[0], "jobs") == 0) { // jobs built-in command
        if (num_args > 1) {
            printf("jobs: too many arguments\n");
            return;
        }
        print_jobs();
    } else if (strcmp(args[0], "fg") == 0) { // fg built-in command
        if (num_args > 2) {
            printf("fg: too many arguments\n");
            return;
        }
        int id = num_jobs;
        if (num_args == 2) {
            id = atoi(args[1]);
            if (id <= 0 || id > num_jobs) {
                printf("fg: invalid job id\n");
                return;
            }
        }
        job j = jobs[id - 1];
        remove_job(id);
        set_foreground(j.pid);
    } else if (strcmp(args[0], "bg") == 0) { // bg built-in command
        if (num_args > 2) {
            printf("bg: too many arguments\n");
            return;
        }
        int id = num_jobs;
        if (num_args == 2) {
            id = atoi(args[1]);
            if (id <= 0 || id > num_jobs) {
                printf("bg: invalid job id\n");
                return;
            }
        }
        job j = jobs[id - 1];
        remove_job(id);
        set_background(j.pid);
    } else { // Else, execute command
        int i;
        int is_pipeline = 0;
        // Check if pipeline
        for (i = 0; i < num_args; i++) {
            if (strcmp(args[i], "|") == 0) {
                is_pipeline = 1;
                break;
            }
        }
        if (is_pipeline) { // Execute pipeline
            execute_pipeline(args, num_args, 0);
        } else { // Execute command
            execute_command(args, num_args, 0);
        }
    }
}

void run_shell(int is_batch) {
    // Set signal handlers
    signal(SIGINT, handle_signal); // Ctrl-C
    signal(SIGTSTP, handle_signal); // Ctrl-Z
    if (!is_batch) {
        printf("wsh> ");
    }
    char* line = NULL;
    size_t line_size = 0;

    // While there is a line to read, execute it and print prompt again
    while (getline(&line, &line_size, stdin) != -1) {
        execute_line(line, is_batch);
        if (!is_batch) {
            printf("wsh> ");
        }
    }
    free(line);
}

int main(int argc, char** argv) {
    if (argc > 2) {
        printf("wsh: too many arguments\n");
        return -1;
    }
    if (argc == 2) {
        int fd = open(argv[1], O_RDONLY);
        if (fd == -1) {
            perror("open");
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2");
            return -1;
        }
        close(fd);
        run_shell(1);
    } else {
        run_shell(0);
    }
    return 0;
}