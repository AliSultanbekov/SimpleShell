#ifndef ALIAS_MAP_H
#define ALIAS_MAP_H

#include <stdint.h>
#include <stdint.h>

#define BUF_SIZE 512

typedef struct{
    char *key;
    char *value;
} AliasEntry;

typedef struct {
    char *args[BUF_SIZE];
    uint32_t argc;
} Decrypted;

int alias_set(char *key, char *value);
int alias_getindex(char *key);
char *alias_get(char *key);
AliasEntry *alias_getall();
void alias_destroy(void);
char *alias_encrypt(char **args, uint32_t argc);
Decrypted alias_decrypt(char *encrypted);
int alias_getsize();

#endif