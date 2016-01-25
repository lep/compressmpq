#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <ctype.h>

const uint32_t HashOffset = 0,
               HashA = 1,
               HashB = 2,
               TableKey = 3;


static uint32_t cryptTable[0x500];


uint32_t hash(const char *path, uint32_t type){
    unsigned char *key = (unsigned char *)path;
    uint32_t seed1 = 0x7FED7FED, seed2 = 0xEEEEEEEE;
    int ch;

    while(*key != 0){
        ch = toupper(*key++);

        seed1 = cryptTable[(type << 8) + ch] ^ (seed1 + seed2);
        seed2 = ch + seed1 + seed2 + (seed2 << 5) + 3; 
    }
    return seed1; 
}

void DecryptBlock(void *block, size_t length, uint32_t key){
    uint32_t seed = 0xEEEEEEEE;
    uint32_t ch;
    uint32_t *castBlock = (uint32_t *)block;

    // Round to longs
    length /= sizeof(uint32_t);

    while(length --> 0){
        seed += cryptTable[0x400 + (key & 0xFF)];
        ch = *castBlock ^ (key + seed);

        key = ((~key << 0x15) + 0x11111111) | (key >> 0x0B);
        seed = ch + seed + (seed << 5) + 3;
        *castBlock++ = ch; 
    }
}
void EncryptBlock(void *block, uint32_t length, uint32_t key){

    uint32_t *castBlock = (uint32_t *)block;
    uint32_t seed = 0xEEEEEEEE;
    uint32_t ch;

    length /= sizeof(uint32_t);

    while(length-- > 0){
        seed += cryptTable[0x400 + (key & 0xFF)];
        ch = *castBlock ^ (key + seed);

        key = ((~key << 0x15) + 0x11111111) | (key >> 0x0B);
        seed = *castBlock + seed + (seed << 5) + 3;

		*castBlock++ = ch;
    }
}


void PrepareCryptTable(){ 
    uint32_t seed = 0x00100001, index1 = 0, index2 = 0, i;

    for(index1 = 0; index1 < 0x100; index1++){
        for(index2 = index1, i = 0; i < 5; i++, index2 += 0x100){
            uint32_t temp1, temp2;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp1 = (seed & 0xFFFF) << 0x10;

            seed = (seed * 125 + 3) % 0x2AAAAB;
            temp2 = (seed & 0xFFFF);

            cryptTable[index2] = (temp1 | temp2); 
        }
    }
}

#endif