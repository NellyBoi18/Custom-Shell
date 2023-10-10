#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 64
#define MAX_NUM_TOKENS 64
#define MAX_JOBS 64

/*
struct job {
    int id;
    pid_t pid;
    char *cmd;
    int is_background;
};
*/

void print_prompt();
void print_jobs();
// void add_job(pid_t pid, char *cmd, int is_background);
void remove_job(int id);
void handle_signal(int signo);
int is_builtin_cmd(char *cmd);
int execute_builtin_cmd(char **tokens);
int execute_cmd(char **tokens, int is_background);
int execute_pipe(char **tokens1, char **tokens2);
int tokenize(char *input, char **tokens);