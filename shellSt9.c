#include <asm-generic/errno-base.h>
#include <errno.h>
#include <linux/limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include "alias.h"

extern char **environ;

#define STATIC_ASSERT(...) _Static_assert(__VA_ARGS__)

#define BUF_SIZE 512
STATIC_ASSERT(BUF_SIZE < UINT32_MAX);

#define ARRAY_LEN(x) (sizeof((x))/sizeof((x)[0]))

#define LOG_ERR(fmt, ...) \
	printf("file: %s line: %d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#define BUG_ON(cond) ({                 \
    bool ret = false;                   \
    if ((cond)) {                       \
        LOG_ERR("BUG: %s\n", #cond);    \
        ret = true;                     \
    }                                   \
                                        \
    ret;                                \
})

#define HISTORY_SIZE 20 //How many commands are being saved
#define HIST_FILENAME ".hist_list"  //stage 6: file in home dir for persistent history

// stage 9
// cycle detection: track which cmd names we've expanded in seen[]; if we see the same again error
// also cap depth so we don't recurse infinatley
#define MAX_SUBSTITUTION_DEPTH 10

//Im going with the structure approach for stage 5 so defining the struct with int and string

typedef struct{
    int index;
    char command[BUF_SIZE];
} CommandHistory;

//Declare Variables
static CommandHistory history[HISTORY_SIZE];  //array of strcuture to hold history
static int total_history = 0; //variable to store how many commands are currently being saved to be used for validation
static int next = 0;   //Variable to point to the next available position in the array

static char *original_path = NULL;

static int process_inp(char *inp);
static void load_history(void);
static void save_history(void);
static void preprocess_cmd(char *cmd, size_t len);
static uint32_t tokenize_into(char *str, char *out_args[BUF_SIZE], uint32_t max_count);

// resolve alias/history until we hit built-in or external
static int handle_cmd(char *args[BUF_SIZE], uint32_t argc, uint32_t depth, char *seen[MAX_SUBSTITUTION_DEPTH]);

static int handle_exit(char *args[BUF_SIZE], uint32_t argc)
{
    if (argc > 2) {
        fprintf(stderr, "exit: too many arguments\n");
        return -EINVAL;
    }

    long code = EXIT_SUCCESS;
    if (argc == 2) {

        char *endptr;   

        errno = 0;
        code = strtol(args[1], &endptr, 10);

        if (args[1] == endptr || errno == ERANGE) {
            fprintf(stderr, "exit: invalid args\n");
            return -EINVAL;
        }
    }

    /* not needed but good practise anyway */
    save_history();
    save_aliases();
    setenv("PATH", original_path, 1);
    free(original_path);

    exit(code);
    BUG_ON(true);
    return -EFAULT;
}

static int handle_cd(char *args[BUF_SIZE], uint32_t argc)
{
    if (argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return -EINVAL;
    }

    char *path = NULL;

    if (argc == 1) {

        path = getenv("HOME");
        if (!path) {
            fprintf(stderr, "cd: no such directory 'home'\n");
            return -EINVAL;
        }

    } else {
        path = args[1];        
    }

    if (chdir(path) < 0) {
        fprintf(stderr, "cd: no such directory '%s'\n", path);
        return -EINVAL;
    }

    return 0;
}

static int handle_getpath(char *args[BUF_SIZE], uint32_t argc)
{
    if (argc != 1) {
        fprintf(stderr, "getpath: too many arguments\n");
        return -EINVAL;
    }

    char *path = getenv("PATH");
    if (!path) {
        fprintf(stderr, "getpath: PATH does not exist\n");
        return -EINVAL;
    }

    printf("%s\n", path);
    return 0;
}

static int handle_setpath(char *args[BUF_SIZE], uint32_t argc)
{
    if (argc != 2) {
        fprintf(stderr, "setpath: too many arguments\n");
        return -EINVAL;
    }

    if (setenv("PATH", args[1], 1) < 0) {
        fprintf(stderr, "setpath: failed to set PATH\n");
        return -EFAULT;
    }

    return 0;
}

static int print_history(char *args[BUF_SIZE], uint32_t argc){  //function that prints all 20 previous commads and their indexes

    if (argc != 1) {
        fprintf(stderr, "history: too many arguments\n");
        return -EINVAL;
    }

    if (total_history == 0){
        printf("No history.\n");
        return 0;
    } //validates if ther is any history to print

    int start = 0; //Initialises variable to mark the oldest command

    int count = 0;
    if (total_history < HISTORY_SIZE)
    {
        count = total_history;
    }
    else
    {
        count = HISTORY_SIZE;
    }

    if (total_history > HISTORY_SIZE){  //find the oldest command if looped round
        start = next;
    }

    for( int i = 0; i < count; i++){
        int j = (start + i) % HISTORY_SIZE;
        printf("%d %s\n", history[j].index, history[j].command);
    } //Loop to print all indexes and commands

    return 0;
}

// get history array index for a history spec (!!, !n, !-n) returns index or -1 on error
static int get_history_command_index(char *cmd)
{
    if ((strcmp(cmd, "!!") == 0)) {
        if (total_history == 0) {
            fprintf(stderr, "no commands in history\n");
            return -1;
        }
        if (total_history <= HISTORY_SIZE)
            return total_history - 1;
        return (next + HISTORY_SIZE - 1) % HISTORY_SIZE;
    }
    if (cmd[1] == '-') {
        char *endptr;
        long specified_number = strtol(cmd + 2, &endptr, 10);
        if (*endptr != '\0' || specified_number <= 0 || specified_number > total_history) {
            fprintf(stderr, "requested history reference out of bounds\n");
            return -1;
        }
        if (total_history <= HISTORY_SIZE)
            return (int)(total_history - specified_number);
        return (next - (int)specified_number + HISTORY_SIZE) % HISTORY_SIZE;
    }
    if (isdigit(cmd[1])) {
        char *endptr;
        long specified_number = strtol(cmd + 1, &endptr, 10);
        if (*endptr != '\0' || specified_number > total_history || specified_number <= 0) {
            fprintf(stderr, "requested history reference out of bounds\n");
            return -1;
        }
        int start = total_history > HISTORY_SIZE ? next : 0;
        int count = total_history < HISTORY_SIZE ? total_history : HISTORY_SIZE;
        for (int i = 0; i < count; i++) {
            int j = (start + i) % HISTORY_SIZE;
            if (history[j].index == (int)specified_number)
                return j;
        }
        return -1;
    }
    fprintf(stderr, "invalid command structure\n");
    return -1;
}

// expand history invocation to full command string (history cmd + extra args)
static int expand_history_to_cmd(char *args[BUF_SIZE], uint32_t argc, char *out_buf, size_t out_size)
{
    if (argc < 1)
        return -EINVAL;
    int idx = get_history_command_index(args[0]);
    if (idx < 0)
        return -EINVAL;
    const char *hist_cmd = history[idx].command;
    if (argc == 1) {
        snprintf(out_buf, out_size, "%s", hist_cmd);
        printf("%s\n", hist_cmd);
        return 0;
    }
    size_t n = (size_t)snprintf(out_buf, out_size, "%s", hist_cmd);
    for (uint32_t i = 1; i < argc && n < out_size; i++) {
        n += (size_t)snprintf(out_buf + n, out_size - n, " %s", args[i]);
    }
    printf("%s\n", out_buf);
    return 0;
}

static int handle_history_request(char *args[BUF_SIZE], uint32_t argc, char* cmd)
{
    if (argc != 1) {
        fprintf(stderr, "too many arguments\n");
        return -EINVAL;
    }

    if ((strcmp(cmd, "!!") == 0)) //execute last command
    {
        if (total_history == 0)
        {
            fprintf(stderr, "no commands in history\n");
            return -EINVAL;
        }
        

        int last_command_index;
        if (total_history <= HISTORY_SIZE)
        {
            // if the history size is not wrapped, last command is at total_history - 1
            last_command_index = total_history - 1;
        }
        else
        {
            // if the history buffer is wrapped around, the last command is at the index before 'next'
            last_command_index = (next + HISTORY_SIZE - 1) % HISTORY_SIZE;
        }

        printf("%s\n", history[last_command_index].command);
        return process_inp(history[last_command_index].command);
        
    }
    else if (cmd[1] == '-') //run command with the number of the current command minus mumber specified
    {
        char *endptr;
        long specified_number = strtol(cmd + 2, &endptr, 10);
        if (*endptr != '\0' || specified_number <= 0 ||specified_number > total_history)
        {
            fprintf(stderr, "requested history reference out of bounds\n");
            return -EINVAL;
        }

        int command_index;
        if(total_history <= HISTORY_SIZE)
        {
            // if the history size is not wrapped, current command is at total_history
            // also take away the specified number
            command_index = (total_history - specified_number);
        }
        else
        {
            // if the history size is wrapped araound, the current command is at the index before 'next'
            // also take away the specified number
            command_index = (next - specified_number + HISTORY_SIZE) % HISTORY_SIZE;
        }

        printf("%s\n", history[command_index].command);
        return process_inp(history[command_index].command);
    }
    else if (isdigit(cmd[1])) //run command with number specified
    {
        char *endptr;
        long specified_number = strtol(cmd + 1, &endptr, 10);
        if (*endptr != '\0' ||specified_number > total_history || specified_number <= 0)
        {
            fprintf(stderr, "requested history reference out of bounds\n");
            return -EINVAL;
        }

        int start = 0; //Initialises variable to mark the oldest command

        int count = 0;
        if (total_history < HISTORY_SIZE)
        {
            count = total_history;
        }
        else
        {
            count = HISTORY_SIZE;
        }

        if (total_history > HISTORY_SIZE){  //find the oldest command if looped round
            start = next;
        }

        //loop through history and find command to run 
        for( int i = 0; i < count; i++){
            int j = (start + i) % HISTORY_SIZE;
            if (history[j].index == specified_number)
            {
                printf("%s\n", history[j].command);
                return process_inp(history[j].command);
            }
        }
        
        return 0;

    }
    else
    {
        fprintf(stderr, "invalid command structure\n");
        return -EINVAL;
    }

    return 0;
}

static int handle_unalias(char *args[BUF_SIZE], uint32_t argc) {
    if (argc != 2) {
        fprintf(stderr, "alias: usage: unalias <name>\n");
        return -EINVAL;
    }

    alias_set(args[1], NULL);

    return 0;
}

static int handle_alias(char *args[BUF_SIZE], uint32_t argc) {
    if (argc == 1) {
        int size = alias_getsize();
        AliasEntry *entry = alias_getall();
        
        for (int i = 0; i < size; i++) {
            fprintf(stderr, "%s : %s\n", entry[i].key, entry[i].value);
        } 
    }
    else if (argc >= 3) {
        char *alias_name = args[1];
        char **save_args = &args[2];
        
        char *param =  alias_encrypt(save_args, argc-2);
        if (!param) {
            fprintf(stderr, "alias: command too large\n");
            return -EINVAL;
        }

        alias_set(alias_name, param);
    } else {
        fprintf(stderr, "alias: usage: alias <name> <command>\n");
        return -EINVAL;
    }

    return 0;
}

static int handle_external(char *args[BUF_SIZE], uint32_t argc)
{
    char *path_env = getenv("PATH");
    if (!path_env) {
        fprintf(stderr, "shell: PATH does not exist\n");
        return -EFAULT;
    }

    char *path = strdup(path_env);
    if (!path) {
        fprintf(stderr, "shell: out of mem\n");
        return -ENOMEM;
    }

    char *save_ptr;
    char *dir = strtok_r(path, ":", &save_ptr);
    char full_path[PATH_MAX];

    /* go along all paths and attempt to execute the bin there, use pipe for 
       detecting if the binary was found and successfully exec'd, this is
       kinda slow though, can optimise this by checking if the binary is there
       before spawning child, this opens up a TOCTOU race though, however it is
       benign, and i cant be bothered doing this rn i need to go work */
    
    while (dir != NULL) {

        int pipefd[2];

        if (pipe(pipefd) < 0) {
            perror("pipe");
            free(path);
            return -EFAULT;
        }

        /* concat the path */
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, args[0]);

        pid_t pid = fork();

        /* fork failed */
        if (pid < 0) {
            perror("fork");
            free(path);
            close(pipefd[0]);
            close(pipefd[1]);
            return -EFAULT;

        /* in child ? */
        } else if (pid == 0) {
       
            /* close read side */
            close(pipefd[0]);

            /* close the pipe on a successful exec call */
            fcntl(pipefd[1], F_SETFD, FD_CLOEXEC); 
            
            execve(full_path, args, environ);

            /* exec failed, not good */
            int err = errno;

            write(pipefd[1], &err, sizeof(err));
            close(pipefd[1]);

            /* _exit in child so we dont flush parents buffers */
            _exit(EXIT_FAILURE);
            BUG_ON(true);

        /* in parent? */
        } else {

            /* close write side */
            close(pipefd[1]);
            
            int child_errno;
            ssize_t n = read(pipefd[0], &child_errno, sizeof(child_errno));
            close(pipefd[0]);

            if (n > 0) { 

                /* exec failed, wait for zombie child to get reaped (prevent 
                   straight up DOSing the host from spawning loads of children),
                   and continue */

                waitpid(pid, NULL, 0); 
                dir = strtok_r(NULL, ":", &save_ptr);
                continue; 
            }

            /* exec succeeded, wait for the child to finish */
            int status;
            waitpid(pid, &status, 0);

            free(path);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
    }

    fprintf(stderr, "command not found: %s\n", args[0]);

    free(path);
    return 0;
}

static int handle_aliased_cmd(char *args[BUF_SIZE], uint32_t argc, uint32_t depth, char *seen[MAX_SUBSTITUTION_DEPTH]);

// buffers for resolving alias/history chains (defined here so handle_cmd can use them)
static char expand_buf[BUF_SIZE];
static char *resolve_arg_buf[BUF_SIZE];
static char *resolve_seen[MAX_SUBSTITUTION_DEPTH];

static int handle_cmd(char *args[BUF_SIZE], uint32_t argc, uint32_t depth, char *seen[MAX_SUBSTITUTION_DEPTH])
{
    BUG_ON(argc == 0);

    // limit substitution depth to catch cycles
    if (depth > MAX_SUBSTITUTION_DEPTH) {
        fprintf(stderr, "shell: too many substitutions (possible cycle)\n");
        return -EINVAL;
    }
    
    char *cmd = args[0];
    size_t cmd_len = strlen(cmd);

    if (alias_getindex(args[0]) != -1) {
        // circular alias check before expanding
        for (uint32_t i = 0; i < depth; i++) {
            if (seen[i] && strcmp(seen[i], args[0]) == 0) {
                fprintf(stderr, "shell: circular alias/history dependency\n");
                return -EINVAL;
            }
        }
        seen[depth] = args[0];
        return handle_aliased_cmd(args, argc, depth, seen);
    }

    if (cmd[0] == '!') {
        // circular history check before expanding
        for (uint32_t i = 0; i < depth; i++) {
            if (seen[i] && strcmp(seen[i], args[0]) == 0) {
                fprintf(stderr, "shell: circular alias/history dependency\n");
                return -EINVAL;
            }
        }
        seen[depth] = args[0];
        // expand history to command string then continue resolving (need it so alias of history works)
        if (expand_history_to_cmd(args, argc, expand_buf, sizeof(expand_buf)) < 0)
            return -EINVAL;
        preprocess_cmd(expand_buf, strlen(expand_buf));
        uint32_t n = tokenize_into(expand_buf, resolve_arg_buf, ARRAY_LEN(resolve_arg_buf));
        if (n == 0)
            return 0;
        return handle_cmd(resolve_arg_buf, n, depth + 1, seen);
    }

    switch (cmd_len) {
        case 2:
            if (strcmp(cmd, "cd") == 0)
                return handle_cd(args, argc);

            break;

        case 4:
            if (strcmp(cmd, "exit") == 0)
                return handle_exit(args, argc);

            break;

        case 5:
            if (strcmp(cmd, "alias") == 0)
                return handle_alias(args, argc);

            break;

        case 7:
            if (strcmp(cmd, "unalias") == 0)
                return handle_unalias(args, argc);
            if (strcmp(cmd, "getpath") == 0)
                return handle_getpath(args, argc);

            if (strcmp(cmd, "setpath") == 0)
                return handle_setpath(args, argc);

            if (strcmp(cmd, "history") == 0)
                return print_history(args, argc);

            break;

        default:
            break;
    }

    return handle_external(args, argc);
}

static int handle_aliased_cmd(char *args[BUF_SIZE], uint32_t argc, uint32_t depth, char *seen[MAX_SUBSTITUTION_DEPTH]) {
    char *e = alias_get(args[0]);
    Decrypted d = alias_decrypt(e);
    
    for (uint32_t i = 1; i < argc; i++) {
        d.args[d.argc] = args[i];
        d.argc++;
    }

    int ret = handle_cmd(d.args, d.argc, depth + 1, seen);
    free(e);
    return ret;
}

/**
 * Preprocess command by replacing delimiters with equivalents
 * This makes parsing commands much easier
 */
static void preprocess_cmd(char *cmd, size_t len)
{
    for (uint32_t i = 0; i < len; i++) {

        if (cmd[i] == '\t')
            cmd[i] = ' ';
    }
}

/* literally impossible for args to go past arg_buf */
static char *arg_buf[BUF_SIZE];
STATIC_ASSERT(ARRAY_LEN(arg_buf) > 1);

// tokenize str into out_args (points into str), return argc; str is modified
static uint32_t tokenize_into(char *str, char *out_args[BUF_SIZE], uint32_t max_count)
{
    uint32_t count = 0;
    char *save_ptr;
    char *token = strtok_r(str, " ", &save_ptr);
    while (token && count < max_count) {
        out_args[count++] = token;
        token = strtok_r(NULL, " ", &save_ptr);
    }
    return count;
}

static int tokenize_cmd(char *cmd, size_t len)
{
    preprocess_cmd(cmd, len);

    uint32_t count = 0;

    char *save_ptr;
    char *token = strtok_r(cmd, " ", &save_ptr);
    while (token) {

        if (count == ARRAY_LEN(arg_buf)) {
            fprintf(stderr, "shell: input too long\n");
            return -EINVAL;
        }

        arg_buf[count] = token;
        count++;

        token = strtok_r(NULL, " ", &save_ptr);
    }

    int ret = 0;
    if (count != 0) {

        arg_buf[count] = NULL;

        // pass depth 0 and seen array for cycle detection
        memset(resolve_seen, 0, sizeof(resolve_seen));
        ret = handle_cmd(arg_buf, count, 0, resolve_seen);
        memset(arg_buf, 0, sizeof(arg_buf));
    }

    return ret;
}

/** 
 * Preprocess input by replacing command delimiters with equivalents and 
   return size 
 * This optimises out the need for strlen, as we can iterate the entire string
   in one go, byte by byte (stride prefetcher friendly!!)
 * This is safe assuming the passed buffer is bounded in size 
 */
static size_t preprocess_inp(char *inp)
{
    uint32_t i = 0;
    while (inp[i] != '\0') {

        if (inp[i] == '\n')
            inp[i] = ';';

        i++;
    }

    return i;
}

/**
 * Processes input by parsing commands, tokenizing commands, and executing the
 * Returns negated errcode on failure, 0 on success
 */
static int process_inp(char *inp)
{
    size_t len = preprocess_inp(inp);

    /* parse commands separately */
    char *save_ptr;
    char *cmd = strtok_r(inp, ";", &save_ptr);
    while (cmd) {

        int err = tokenize_cmd(cmd, strlen(cmd));
        if (err < 0) {
            fprintf(stderr, "%s: failed with status %d\n", cmd, -err);
            return err;
        }

        cmd = strtok_r(NULL, ";", &save_ptr);
    }

	return 0;
}

static void add_history(char *command){  
    //Imput validation (not blank), doesnt save if no input of new line
    if(command[0] == '\0' || command[0] == '\n' || command[0] == '!'){
        return;
    }

    //remove new line when storing command
    if (strlen(command) > 0 && command[strlen(command) - 1] == '\n')
    {
        command[strlen(command) - 1] = '\0';
    }

    history[next].index = total_history + 1; //assigns the next command its index 
    strncpy(history[next].command, command, BUF_SIZE - 1); //Adds the passed command to the array
    history[next].command[BUF_SIZE - 1] = '\0'; //adds null terminator to command
    next = (next + 1) % HISTORY_SIZE; //sets next to the next location (from assignment specs)
    total_history++; //updates the total commands 
}

//load history from .hist_list in root dir on start
static void load_history(void)
{
    char *home = getenv("HOME");
    if (!home) {
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", home, HIST_FILENAME);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        // if file doesnt exist tart with empty history file
        return;
    }

    char line[BUF_SIZE];
    int count = 0;
    int max_index = 0;

    while (fgets(line, sizeof(line), fp) != NULL && count < HISTORY_SIZE) {
        char *endptr;
        long num = strtol(line, &endptr, 10);
        if (endptr == line || num <= 0) {
            continue;
        }
        while (*endptr == ' ' || *endptr == '\t') {
            endptr++;
        }
        if (*endptr == '\0' || *endptr == '\n') {
            continue;
        }
        // strip newline from command end
        size_t len = strlen(endptr);
        if (len > 0 && endptr[len - 1] == '\n') {
            endptr[len - 1] = '\0';
        }

        history[count].index = (int)num;
        if ((int)num > max_index) {
            max_index = (int)num;
        }
        strncpy(history[count].command, endptr, BUF_SIZE - 1);
        history[count].command[BUF_SIZE - 1] = '\0';
        count++;
    }

    fclose(fp);
    total_history = max_index;  // so !n and !-n refer to correct command numbers
    next = count % HISTORY_SIZE;
}

// save history to .hist_list on shell exit
static void save_history(void)
{
    char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "shell: could not get HOME, history not saved\n");
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", home, HIST_FILENAME);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("shell: could not open history file for writing");
        return;
    }

    int start = 0;
    int count = 0;
    if (total_history == 0) {
        fclose(fp);
        return;
    }
    if (total_history < HISTORY_SIZE) {
        count = total_history;
    } else {
        count = HISTORY_SIZE;
        start = next;
    }

    for (int i = 0; i < count; i++) {
        int j = (start + i) % HISTORY_SIZE;
        fprintf(fp, "%d %s\n", history[j].index, history[j].command);
    }

    fclose(fp);
}

/* For doing the rest of part 5
I have gone a bit mental with the comments so hopefully what ive done makes sense and works
if i need to fix it or do more just lmk
What still needs to be done is from the "Invoke command from history" bit on the instructions
im not sure how much work that is so if its a lot I can do more
what ive done will also need to be integrated into the main function */



int main(int argc, char *argv[])
{
    original_path = strdup(getenv("PATH"));
    if (!original_path) {
        fprintf(stderr, "shell: out of mem\n");
        return -ENOMEM;
    }

    load_history();
    load_aliases();

    int ret = EXIT_SUCCESS;

    /* handle cli args */
    if (argc > 1) {
        
    }

    /* add previous line at start of next iteration so !! sees the right last command */
    char prev_buf[BUF_SIZE];
    prev_buf[0] = '\0';

    while (1) {

        if (prev_buf[0] != '\0')
            add_history(prev_buf);

        char buf[BUF_SIZE];
        printf("$ ");
	
        if (!fgets(buf, sizeof(buf), stdin)) {

            if (!feof(stdin)) {
                perror("fgets");
                ret = EXIT_FAILURE;
            }
            /* nl so next shell prompt appears on its own line after ctrl d */
            printf("\n");
            goto done;
        }
        strncpy(prev_buf, buf, BUF_SIZE - 1);
        prev_buf[BUF_SIZE - 1] = '\0';
        process_inp(buf);
    }

done:
    save_history();
    save_aliases();
    setenv("PATH", original_path, 1);
    free(original_path);
    return ret;
}





