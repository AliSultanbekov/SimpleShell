// fathers work
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#define MAX_ALIASES 512
#define BUF_SIZE 512

#define ALIS_FILENAME ".alis_list"

typedef struct{
    char *key;
    char *value;
} AliasEntry;

typedef struct{
    char *args[BUF_SIZE];
    uint32_t argc;
} Decrypted;

static AliasEntry alias_map[MAX_ALIASES];
static int map_size = 0;

void freeindex(int index) {
    free(alias_map[index].key);
    free(alias_map[index].value);
    alias_map[index].key = NULL;
    alias_map[index].value = NULL;
}

int alias_getindex(char *key) {
    for (int i = 0; i < map_size; i++) {
        if (strcmp(alias_map[i].key, key) == 0) {
            return i;
        }
    }
    
    return -1;
}

char *alias_encrypt(char *args[BUF_SIZE], uint32_t argc) {
    char encrypted[BUF_SIZE] = {0};

    size_t n = 0;

    for (uint32_t i = 0; i < argc; i++) {

        size_t len = strlen(args[i]);
        if ((n + len) >= sizeof(encrypted))
            return NULL;

        strcat(encrypted, args[i]);
        n += len;
        
        if (i < argc-1) {

            size_t len = strlen(" ");
            if ((n + len) >= sizeof(encrypted))
                return NULL;

            strcat(encrypted, " ");
            n += len;
        }
    }

    return strdup(encrypted);
}

Decrypted alias_decrypt(char *encrypted) {
    Decrypted d = {0};

    char *save_ptr;
    char *token = strtok_r(encrypted, " ", &save_ptr);

    while (token) {
        d.args[d.argc] = token;
        d.argc++;
        token = strtok_r(NULL, " ", &save_ptr);
    }

    return d;
}

int alias_set(char *key, char *value) {
    int entry_index = alias_getindex(key);

    if (value == NULL) {
        if (map_size <= 0) {
            return -1;
        }

        if (entry_index == -1) {
            return -1;
        }

        freeindex(entry_index);

        for (int i = entry_index; i < map_size - 1; i++) {
            alias_map[i] = alias_map[i+1];
        }

        map_size--;
        return 0;
    }

    if (entry_index == -1) {
        if (map_size >= MAX_ALIASES) {
            return -1;
        }

        alias_map[map_size].key = strdup(key);
        alias_map[map_size].value = strdup(value);

        if (alias_map[map_size].key == NULL || alias_map[map_size].value == NULL) {
            freeindex(map_size);
            return -1;
        }

        map_size++;
    } else {
        char *new_value = strdup(value);

        if (new_value == NULL) {
            return -1;
        }

        free(alias_map[entry_index].value);
        alias_map[entry_index].value = new_value;
    }

    return 0;
}

char *alias_get(char *key) {
    if (map_size <= 0) {
        return NULL;
    }

    int entry_index = alias_getindex(key);

    if (entry_index == -1) {
        return NULL;
    }

    return strdup(alias_map[entry_index].value);
}

AliasEntry *alias_getall() {
    return alias_map;
}

int alias_getsize() {
    return map_size;
}

void alias_destroy() {
    for (int i = 0; i < map_size; i++) {
        freeindex(i);
    }

    map_size = 0;
}

void save_aliases()
{
    char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "shell: could not get HOME, aliases not saved\n");
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", home, ALIS_FILENAME);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        perror("shell: could not open aliases file for writing");
        return;
    }

    for (int i = 0; i < map_size; i++)
    {
        fprintf(fp, "%s %s\n", alias_map[i].key, alias_map[i].value);
    }

    fclose(fp);
}

//for load i largely used the same structure as save
void load_aliases()
{

    //find and open file
    char *home = getenv("HOME");
    if (!home) {
        fprintf(stderr, "shell: could not get HOME\n");
        return;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", home, ALIS_FILENAME);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("shell: could not open aliases file for reading");
        return;
    }

    char line[BUF_SIZE];

    while(fgets(line, sizeof(line), fp)){

        //looks for new line
        char *newline = strchr(line, '\n');


        //replace newline with terminator
        if(newline) {
            *newline = '\0';
        }

        char *pointer;
        char *key = strtok_r(line, " ", &pointer);
        char *value = pointer;
        alias_set(key, value);



    }

    fclose(fp);
}





