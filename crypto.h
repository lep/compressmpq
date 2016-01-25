#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <ctype.h>

extern const uint32_t HashOffset, HashA, HashB, TableKey;


uint32_t hash(const char *path, uint32_t type);
void DecryptBlock(void *block, size_t length, uint32_t key);
void EncryptBlock(void *block, uint32_t length, uint32_t key);
void PrepareCryptTable();


#endif
