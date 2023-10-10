#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 256
#define MAX_NUM_TOKENS 256
#define MAX_JOBS 256

char *path[] = {"/bin", NULL};
char *built_in_cmds[] = {"exit", "cd", "jobs", "fg", "bg"};

struct job {
    int id;
    pid_t pid;
    char *cmd;
    int is_background;
};

struct job jobs[MAX_JOBS];
int num_jobs = 0;

void print_prompt() {
    printf("wsh> ");
    fflush(stdout); // Flushes the output buffer of a stream for it to be written immediately
}

void print_jobs() {
    for (int i = 0; i < num_jobs; i++) {
        printf("[%d] %s %s\n", jobs[i].id, jobs[i].cmd, jobs[i].is_background ? "&" : "");
    }
}

void add_job(pid_t pid, char *cmd, int is_background) {
    jobs[num_jobs].id = num_jobs + 1;
    jobs[num_jobs].pid = pid;
    jobs[num_jobs].cmd = cmd;
    jobs[num_jobs].is_background = is_background;
    num_jobs++;
}

void remove_job(int id) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].id == id) {
            for (int j = i; j < num_jobs - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            num_jobs--;
            break;
        }
    }
}

void handle_signal(int signo) {
    if (signo == SIGINT) { // If SIGINT, print newline
        printf("\n");
        print_prompt();
    }
}

int is_builtin_cmd(char *cmd) {
    for (int i = 0; i < sizeof(built_in_cmds) / sizeof(char *); i++) {
        if (strcmp(cmd, built_in_cmds[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int execute_builtin_cmd(char **tokens) {
    if (strcmp(tokens[0], "exit") == 0) { // If exit, close shell
        exit(0);
    } else if (strcmp(tokens[0], "cd") == 0) { // If cd, change directory
        if (tokens[1] == NULL) {
            fprintf(stderr, "wsh: expected argument to \"cd\"\n");
        } else { // Change the cwd of the calling process
            if (chdir(tokens[1]) != 0) {
                perror("cd");
            } 
        }
        return 1;
    } else if (strcmp(tokens[0], "jobs") == 0) { // If jobs, print jobs
        print_jobs();
        return 1;
    } else if (strcmp(tokens[0], "fg") == 0) { // If fg, bring job to foreground
        int id = num_jobs;
        if (tokens[1] != NULL) {
            id = atoi(tokens[1]);
        }
        if (id <= 0 || id > num_jobs) {
            fprintf(stderr, "wsh: invalid job id\n");
        } else {
            pid_t pid = jobs[id - 1].pid;
            remove_job(id);
            tcsetpgrp(STDIN_FILENO, getpgid(pid));
            kill(-pid, SIGCONT);
            int status;
            waitpid(pid, &status, WUNTRACED);
            if (WIFSTOPPED(status)) {
                add_job(pid, jobs[id - 1].cmd, 1);
            }
            tcsetpgrp(STDIN_FILENO, getpgrp());
        }
        return 1;
    } else if (strcmp(tokens[0], "bg") == 0) { // If bg, bring job to background
        int id = num_jobs;
        if (tokens[1] != NULL) {
            id = atoi(tokens[1]);
        }
        if (id <= 0 || id > num_jobs) {
            fprintf(stderr, "wsh: invalid job id\n");
        } else {
            pid_t pid = jobs[id - 1].pid;
            jobs[id - 1].is_background = 1;
            kill(-pid, SIGCONT);
        }
        return 1;
    }
    return 0;
}

int execute_cmd(char **tokens, int is_background) {
    pid_t pid = fork();
    // printf("pid: %d\n", pid);
    if (pid == 0) { // Child process
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        if (execvp(tokens[0], tokens) == -1) {
            perror("wsh");
            exit(-1);
        }
    } else if (pid < 0) { // Fork error
        perror("wsh");
    } else { // Parent process
        if (!is_background) {
            int status;
            waitpid(pid, &status, WUNTRACED);
            if (WIFSTOPPED(status)) { // If child process was stopped, add it to the list of background jobs
                add_job(pid, tokens[0], 1);
            }
        } else {
            add_job(pid, tokens[0], 1);
        }
    }
    return 1;
}

int execute_pipe(char **tokens1, char **tokens2) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("wsh");
        return 1;
    }
    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        if (execvp(tokens1[0], tokens1) == -1) {
            perror("wsh");
            exit(-1);
        }
    } else if (pid1 < 0) {
        perror("wsh");
    } else {
        pid_t pid2 = fork();
        if (pid2 == 0) {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            if (execvp(tokens2[0], tokens2) == -1) {
                perror("wsh");
                exit(-1);
            }
        } else if (pid2 < 0) {
            perror("wsh");
        } else {
            close(pipefd[0]);
            close(pipefd[1]);
            int status1, status2;
            waitpid(pid1, &status1, WUNTRACED);
            waitpid(pid2, &status2, WUNTRACED);
            if (WIFSTOPPED(status1)) {
                add_job(pid1, tokens1[0], 1);
            }
            if (WIFSTOPPED(status2)) {
                add_job(pid2, tokens2[0], 1);
            }
        }
    }
    return 1;
}

int tokenize(char *input, char **tokens) {
    int num_tokens = 0;
    char *token = strtok(input, " \t\r\n\a");
    while (token != NULL) {
        tokens[num_tokens] = token;
        num_tokens++;
        token = strtok(NULL, " \t\r\n\a");
    }
    tokens[num_tokens] = NULL;
    return num_tokens;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);
    if (argc > 2) {
        fprintf(stderr, "wsh: expected 0 or 1 argument\n");
        exit(-1);
    }
    // Batch File
    if (argc == 2) {
        FILE *fp = fopen(argv[1], "r");
        if (fp == NULL) {
            perror("wsh");
            exit(-1);
        }
        char *line = NULL;
        size_t len = 0;
        ssize_t nread;
        while ((nread = getline(&line, &len, fp)) != -1) {
            line[nread - 1] = '\0';
            char *tokens[MAX_NUM_TOKENS];
            int num_tokens = tokenize(line, tokens);
            if (num_tokens > 0) {
                if (is_builtin_cmd(tokens[0])) {
                    execute_builtin_cmd(tokens);
                } else {
                    execute_cmd(tokens, 0);
                }
            }
        }
        free(line);
        fclose(fp);
        exit(0);
    }

    // Interactive Mode
    while (1) {
        print_prompt();
        char input[MAX_INPUT_SIZE];
        if(fgets(input, MAX_INPUT_SIZE, stdin)) { // If there is a line to read, execute it and print prompt again
            input[strlen(input) - 1] = '\0';
            char *tokens1[MAX_NUM_TOKENS];
            char *tokens2[MAX_NUM_TOKENS];
            int num_tokens1 = 0;
            int num_tokens2 = 0;
            int is_pipe = 0;
            char *token = strtok(input, "|");
            while (token != NULL) {
                char *tokens[MAX_NUM_TOKENS];
                int num_tokens = tokenize(token, tokens);
                if (num_tokens > 0) {
                    if (is_pipe) {
                        num_tokens2 = num_tokens;
                        for (int i = 0; i < num_tokens; i++) {
                            tokens2[i] = tokens[i];
                        }
                    } else {
                        num_tokens1 = num_tokens;
                        for (int i = 0; i < num_tokens; i++) {
                            tokens1[i] = tokens[i];
                        }
                    }
                }
                is_pipe = 1;
                token = strtok(NULL, "|");
            }
            if (num_tokens1 > 0) {
                if (is_builtin_cmd(tokens1[0])) {
                    execute_builtin_cmd(tokens1);
                } else if (num_tokens2 > 0) {
                    execute_pipe(tokens1, tokens2);
                } else {
                    execute_cmd(tokens1, 0);
                }
            }
        }
    }
    return 0;
}