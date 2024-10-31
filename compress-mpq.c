#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <sys/types.h>
#include <utime.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h>   // for nanosleep
#else
#include <unistd.h> // for usleep
#endif

#include "zopfli/zopfli.h"
#include "miniz.h"
#include "Adpcm/adpcm.h"
#include "Huffman/huff-c.h"
#include "Pklib/pklib.h"

#include "thread.h"
#include "table.h"
#include "crypto.h"
#include "queue.h"
#include "listfile.h"
#include "lonesha256.h"

#if defined(_WIN32)
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

#define FLAG_FILE_ENCRYPTED     (0x00010000)
#define FLAG_FILE_KEY_ADJUSTED  (0x00020000)
#define FLAG_FILE_SINGLE_UNIT   (0x01000000)
#define FLAG_FILE_COMPRESSED    (0x00000200)
#define FLAG_FILE_EXISTS        (0x80000000)

struct header {
    char magic[4];
    uint32_t headerSize;
    uint32_t archiveSize;
    uint16_t version;
    uint16_t shift;
    uint32_t htPos;
    uint32_t btPos;
    uint32_t htSize;
    uint32_t btSize;
} __attribute__((packed));

typedef struct header header_t;

struct {
    FILE *mpq_file;
    sys_lock_t lock;
    table_t mpq_table;
    queue_t work_queue;
    size_t bytesWritten;
    size_t totalInSize;
    size_t totalOutSize;
    size_t filesProceeded;
    
    size_t mpqShift;
    size_t blockSize;
    int useCache;
    ZopfliOptions zopfli_options;
    
    struct {
        char *file;
        size_t size;
        char *mpq;
        header_t hd;
        table_t tbl;
        uint32_t offset;
    } inMpq;
    
    listfile_t listfile;
} globals;

static const char Base64URLTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void sleep_ms(int milliseconds){ // cross-platform sleep function
#ifdef WIN32
    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    if (milliseconds >= 1000)
      sleep(milliseconds / 1000);
    usleep((milliseconds % 1000) * 1000);
#endif
}

int IsDelim(char c, char *delim){
    for(; *delim; delim++){
        if(c == *delim)
            return 1;
    }
    return 0;
}

char* strtok_l(char *in, char *delim, size_t size){
    static size_t idx = 0;
    static char *input = NULL;
    
    if(in != NULL){
        idx = 0;
        input = in;
    }

    for(; idx != size; idx++){
        if(IsDelim(input[idx], delim))
            input[idx] = 0;
        else
            break;
    }
    
    size_t start = idx;
    
    for(; idx < size; idx++){
        if(IsDelim(input[idx], delim)){
            input[idx] = 0;
            idx++;
            return input+start;
        }else if(idx == size-1){
            idx++;
            return input+start;
        }
    }
    return NULL;
}

uint32_t FindHeader(char *file, size_t size, header_t *hd){
    for(uint32_t offset = 0; offset < size; offset += 0x200){
        if(!strncmp("MPQ\x1a", file+offset, 4)){
            *hd = *(header_t*)(file+offset);
            return offset;
        }
    }
    return (uint32_t)(-1);
}

btentry_t* FindBTE(table_t *tbl, const char *path){
    uint32_t hashA = hash(path, HashA),
             hashB = hash(path, HashB),
             start = hash(path, HashOffset) % tbl->htSize,
             pos = start;
    htentry_t *ht = tbl->ht;
    while(ht[pos].blockIndex != 0xFFFFFFFF){
        if(ht[pos].hashA == hashA && ht[pos].hashB == hashB){
            return &tbl->bt[ht[pos].blockIndex];
        }
        pos = (pos+1) % tbl->htSize;
        if(pos == start){
            return NULL;
        }
    }

    return NULL;
}

static void WriteInt(unsigned char *out, size_t off, uint32_t n){
    out[off++] = n & 0xFF;
    out[off++] = (n >> 8) & 0xFF;
    out[off++] = (n >> 16) & 0xFF;
    out[off++] = (n >> 24) & 0xFF;
}

char* GetFileName(char *path){
    char *name = strrchr(path, '\\');
    if(name)
        return name+1;
    else
        return path;
}

enum DecompressError {
    Ok = 0,
    ZlibError = 1,
    HuffmanError = 2,
    PklibError = 3,
    AdpcmError = 4
};

int decompress(void *outBuf, size_t *outLen, void *inBuf, size_t inSize){
    uint8_t whatComp = *(uint8_t*)inBuf;
    inBuf++;
    void *tmpBuf = outBuf;
    size_t *tmpLen = outLen;
    if(whatComp & 0x02){
//int mz_uncompress(unsigned char *pDest, mz_ulong *pDest_len, const unsigned char *pSource, mz_ulong source_len);
        int err = mz_uncompress(tmpBuf, (mz_ulong*)tmpLen, inBuf, (mz_ulong)inSize);
        if(MZ_OK != err){
            return ZlibError;
        }
        inBuf = tmpBuf;
        inSize = *tmpLen;
    }
    if(whatComp & 0x08){
//int DecompressPKLIB(void * pvOutBuffer, int * pcbOutBuffer, void * pvInBuffer, int cbInBuffer);
        if(0 == DecompressPKLIB(tmpBuf, (int*)tmpLen, inBuf, inSize)){
            return PklibError;
        }
        inBuf = tmpBuf;
        inSize = *tmpLen;
    }
    if(whatComp & 0x40){
//int  DecompressADPCM(void * pvOutBuffer, int dwOutLength, void * pvInBuffer, int dwInLength, int ChannelCount);
        size_t outSizeTmp = DecompressADPCM(tmpBuf, *tmpLen, inBuf, inSize, 1);
        if(0 == outSizeTmp){
            return AdpcmError;
        }
		inBuf = tmpBuf;
		*tmpLen = outSizeTmp;
        inSize = *tmpLen;
    }
    if(whatComp & 0x80){
//int  DecompressADPCM(void * pvOutBuffer, int dwOutLength, void * pvInBuffer, int dwInLength, int ChannelCount);
        size_t outSizeTmp = DecompressADPCM(tmpBuf, *tmpLen, inBuf, inSize, 2);
        if(0 == outSizeTmp){
            return AdpcmError;
        }
		inBuf = tmpBuf;
		*tmpLen = outSizeTmp;
        inSize = *tmpLen;
	}
    if(whatComp & 0x01){
//int DecompressHuffman(void * pvOutBuffer, int * pcbOutBuffer, void * pvInBuffer, int cbInBuffer);
		if(0 == DecompressHuffman(tmpBuf, (int*)tmpLen, inBuf, inSize)){
            return HuffmanError;
        }
		inBuf = tmpBuf;
		inSize = *tmpLen;
	}

    return Ok;

}
//int DecompressHuffman(void * pvOutBuffer, int * pcbOutBuffer, void * pvInBuffer, int cbInBuffer);

char* ExtractFile(char *mpq, header_t *hd, table_t *tbl, char *path, size_t *out){
    btentry_t *bte = FindBTE(tbl, path);
    if(!bte){
        return NULL;
    }
    uint32_t baseKey = hash(GetFileName(path), TableKey);
    int encrypted = bte->flags & FLAG_FILE_ENCRYPTED;
    if(bte->flags & FLAG_FILE_KEY_ADJUSTED)
        baseKey = (baseKey + bte->filePos) ^ bte->normalSize;
    
    char *file = malloc(bte->normalSize);
    char *fileInMpq = mpq+bte->filePos;
    if(out)
        *out = bte->normalSize;
    size_t destLen;

    if(bte->flags & FLAG_FILE_SINGLE_UNIT && !(bte->flags & FLAG_FILE_COMPRESSED)){
        memcpy(file, fileInMpq, bte->normalSize);
        if(encrypted)
            DecryptBlock(file, bte->normalSize, baseKey);
    }else if(!(bte->flags & FLAG_FILE_COMPRESSED) && !(bte->flags & FLAG_FILE_SINGLE_UNIT)){
        uint32_t sectorSize = 512 * (1 << hd->shift);
        size_t numSectors = (size_t)ceil((float)bte->normalSize / sectorSize);
        for(size_t i = 0, offset = 0; i != numSectors; i++, offset += sectorSize){
            uint32_t thisSize = sectorSize;
            if(i == numSectors-1)
                thisSize = bte->normalSize % sectorSize;
            memcpy(file+offset, fileInMpq+offset, thisSize);
            DecryptBlock(file+offset, thisSize, baseKey+i);
        }
    }else if(bte->flags & FLAG_FILE_SINGLE_UNIT && bte->flags & FLAG_FILE_COMPRESSED){
        if(encrypted)
            DecryptBlock(file, bte->compressedSize, baseKey);

        int err = decompress(file, &destLen, fileInMpq, bte->compressedSize-1);
        if(Ok != err){
            fprintf(stderr, "Error while decompressing '%s' (%d, %d)\n", path, *fileInMpq, err);
            free(file);
            return NULL;
        }
        
    }else{
        // we've got a sector offset table
        uint32_t sectorSize = 512 * (1 << hd->shift);
        size_t numSectors = (size_t)(1+ceil((float)(bte->normalSize) / sectorSize));
        uint32_t *sectorOffsetTable = (uint32_t*)fileInMpq;
        size_t offset = 0;

        if(encrypted)
            DecryptBlock(sectorOffsetTable, numSectors*sizeof(uint32_t), baseKey-1);
        
        for(size_t idx = 0; idx != numSectors-1; idx++){
            uint32_t size = sectorOffsetTable[idx+1] - sectorOffsetTable[idx];
            uint32_t thisSectorSize = sectorSize;
            
            // in case of strange errors: check this
            if(idx == numSectors -2) // last sector so the size can be less than sectorSize
                thisSectorSize = bte->normalSize % sectorSize;
            if(thisSectorSize == 0)
                thisSectorSize = sectorSize;
            destLen = thisSectorSize;
            
            if(encrypted)
                DecryptBlock(fileInMpq+sectorOffsetTable[idx], size, baseKey+idx);
            
            if(size == thisSectorSize){
                // this sector is not compressed
                memcpy(file+offset, fileInMpq+sectorOffsetTable[idx], size);
            }else{
                int err = decompress(file+offset, &destLen, fileInMpq+sectorOffsetTable[idx], size);
                if(Ok != err){
                    fprintf(stderr, "Error while decompressing '%s' (%d, %d)\n", path, *(fileInMpq+sectorOffsetTable[idx]), err);
                    free(file);
                    return NULL;
                }
            }
            offset += sectorSize;
        }
    }
    
    return file;
}


static char* Sys_ReadFile(const char *path, size_t *insize){
    FILE *fh = fopen(path, "rb");
    if(fh == NULL){
        fprintf(stderr, "Couldn't open file '%s'\n", path);
        exit(1);
    }
    fseek(fh, 0, SEEK_END);
    long s = ftell(fh);
    rewind(fh);
    char *buffer = malloc(s);
    fread(buffer, s, 1, fh);
    fclose(fh);
    *insize = s;
    return buffer;
}

void WriteHT(){
    if(globals.mpq_table.htSize == 0)
        return;
    unsigned char *ht = malloc(16*globals.mpq_table.htSize);
    for(uint32_t i = 0; i != globals.mpq_table.htSize; i++){
        uint32_t idx = i*16;
        WriteInt(ht, idx, globals.mpq_table.ht[i].hashA);
        WriteInt(ht, idx+4, globals.mpq_table.ht[i].hashB);
        WriteInt(ht, idx+8, globals.mpq_table.ht[i]._padding);
        WriteInt(ht, idx+12, globals.mpq_table.ht[i].blockIndex);
    }
    EncryptBlock(ht, 16*globals.mpq_table.htSize, hash("(hash table)", TableKey));
    fwrite(ht, 16*globals.mpq_table.htSize, 1, globals.mpq_file);
}

void WriteBT(){
    if(globals.mpq_table.btSize == 0)
        return;
    unsigned char *bt = malloc(16*(globals.mpq_table.btSize));
    for(uint32_t i = 0; i != globals.mpq_table.btSize; i++){
        uint32_t idx = i*16;
        WriteInt(bt, idx, globals.mpq_table.bt[i].filePos);
        WriteInt(bt, idx+4, globals.mpq_table.bt[i].compressedSize);
        WriteInt(bt, idx+8, globals.mpq_table.bt[i].normalSize);
        WriteInt(bt, idx+12, globals.mpq_table.bt[i].flags);
    }
    EncryptBlock(bt, 16*(globals.mpq_table.btSize), hash("(block table)", TableKey));
    fwrite(bt, 16*(globals.mpq_table.btSize), 1, globals.mpq_file);
}

void WriteHeader(){
    uint32_t htSize = globals.mpq_table.htSize * 16;
    uint32_t btSize = globals.mpq_table.btSize * 16;
    uint32_t archiveSize = globals.bytesWritten + htSize + btSize;
    uint32_t htPos = globals.bytesWritten;
    uint32_t btPos = globals.bytesWritten + htSize;

    unsigned char buffer[4];
    
    fseek(globals.mpq_file, globals.inMpq.offset, SEEK_SET);
    
    fwrite("MPQ\x1a", 4, 1, globals.mpq_file);
    fwrite("\x20\0\0\0", 4, 1, globals.mpq_file);
    
    WriteInt(buffer, 0, archiveSize);
    fwrite(buffer, 4, 1, globals.mpq_file);
    fwrite("\0\0", 2, 1, globals.mpq_file);

    unsigned char shift = (unsigned char)globals.mpqShift;
    fwrite(&shift, 1, 1, globals.mpq_file);
    fwrite("\0", 1, 1, globals.mpq_file);
    
    WriteInt(buffer, 0, htPos);
    fwrite(buffer, 4, 1, globals.mpq_file);
    
    WriteInt(buffer, 0, btPos);
    fwrite(buffer, 4, 1, globals.mpq_file);
    
    WriteInt(buffer, 0, globals.mpq_table.htSize);
    fwrite(buffer, 4, 1, globals.mpq_file);
    
    WriteInt(buffer, 0, globals.mpq_table.btSize);
    fwrite(buffer, 4, 1, globals.mpq_file);
}


void ConvertSlashes(char *path){
    for(int i = 0; path[i]; i++){
        if(path[i]=='/')
            path[i]='\\';
    }
}

void ReadListfile(listfile_t *listfile, table_t *tbl, char *content, size_t size){
    char delim[] = "\r\n;";
    char *result = NULL;
    result = strtok_l(content, delim, size);
    while(result != NULL){
        btentry_t *bte;
        if((bte = FindBTE(tbl, result)) != NULL){
            AddPath(listfile, hash(result, HashA), hash(result, HashB), result);
        }
        result = strtok_l(NULL, delim, size);
    }
}

int ListfileSufficient(table_t *tbl, listfile_t *listfile){
    for(uint32_t i = 0; i != tbl->htSize; i++){
        htentry_t hte = tbl->ht[i];
        if(hte.blockIndex != 0xffffffff && hte.blockIndex != 0xfffffffe){
            if(!FindPath(listfile, hte.hashA, hte.hashB))
                return 0;
        }
    }
    
    return 1;
}


void PackFile(unsigned char *content, size_t contentSize, size_t bufferSize, unsigned char *out, size_t *outsize, uint32_t *flags){
    unsigned char *zopfli_out;
    size_t zopfli_outsize;
    ZopfliFormat format = ZOPFLI_FORMAT_ZLIB;

    size_t written = 0;
    size_t offsetTableSize = 4*(1+ ceil( ((float)contentSize)/globals.blockSize ));
    uint32_t *sectorOffsetTable = malloc(offsetTableSize);
    size_t tableIdx = 1;
    size_t outpos = 0;

    for(size_t start = 0, end = contentSize; start < end; start += globals.blockSize){
        size_t len = end-start > globals.blockSize ? globals.blockSize : end-start;
        zopfli_outsize = 0;
        ZopfliCompress(&globals.zopfli_options, format, content+start, len, &zopfli_out, &zopfli_outsize);
        if(zopfli_outsize < len && zopfli_outsize <= globals.blockSize -2){
            written += 1+zopfli_outsize;
            assert(written < bufferSize);
            out[outpos] = 2;
            memcpy(out+outpos+1, zopfli_out, zopfli_outsize);
            sectorOffsetTable[tableIdx++] = 1+zopfli_outsize;
            outpos += 1+zopfli_outsize;
        }else{
            written += len;
            assert(written < bufferSize);
            memcpy(out+outpos, content+start, len);
            sectorOffsetTable[tableIdx++] = len;
            outpos += len;
        }
        free(zopfli_out);
    }
    

    memmove(out+offsetTableSize, out, written);
    *outsize = written + offsetTableSize;
    *flags = FLAG_FILE_COMPRESSED;
    sectorOffsetTable[0] = offsetTableSize;
    for(size_t i = 0, acc = 0; i != tableIdx; i++){
        acc += sectorOffsetTable[i];
        WriteInt(out, i*4, acc);
    }
    free(sectorOffsetTable);
    
}

// Helper function to encode data in Base64url format (RFC 4648)
void Base64URLEncode(char *encoded, const char *string, int len) {
  /* Original source code taken from
   * https://svn.apache.org/repos/asf/apr/apr/trunk/encoding/apr_base64.c
   *
   * Changes by Michel Lang <michellang@gmail.com>:
   * - Replaced char 62 ('+') with '-'
   * - Replaced char 63 ('/') with '_'
   * - Removed padding with '=' at the end of the string
   * - Changed return type to void for Base64encode
   *
   * Changes by Leonardo Julca <ivojulca@hotmail.com>:
   * - Renamed to Base64URLEncode
   */
  /*
   * base64.c:  base64 encoding and decoding functions
   *
   * ====================================================================
   *    Licensed to the Apache Software Foundation (ASF) under one
   *    or more contributor license agreements.  See the NOTICE file
   *    distributed with this work for additional information
   *    regarding copyright ownership.  The ASF licenses this file
   *    to you under the Apache License, Version 2.0 (the
   *    "License"); you may not use this file except in compliance
   *    with the License.  You may obtain a copy of the License at
   *
   *      http://www.apache.org/licenses/LICENSE-2.0
   *
   *    Unless required by applicable law or agreed to in writing,
   *    software distributed under the License is distributed on an
   *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   *    KIND, either express or implied.  See the License for the
   *    specific language governing permissions and limitations
   *    under the License.
   * ====================================================================
   */

    int i;
    char *p = encoded;

    for (i = 0; i < len - 2; i += 3) {
        *p++ = Base64URLTable[(string[i] >> 2) & 0x3F];
        *p++ = Base64URLTable[((string[i] & 0x3) << 4) | ((int) (string[i + 1] & 0xF0) >> 4)];
        *p++ = Base64URLTable[((string[i + 1] & 0xF) << 2) | ((int) (string[i + 2] & 0xC0) >> 6)];
        *p++ = Base64URLTable[string[i + 2] & 0x3F];
    }

    if (i < len) {
        *p++ = Base64URLTable[(string[i] >> 2) & 0x3F];
        if (i == (len - 1)) {
            *p++ = Base64URLTable[((string[i] & 0x3) << 4)];
        } else {
            *p++ = Base64URLTable[((string[i] & 0x3) << 4) | ((int) (string[i + 1] & 0xF0) >> 4)];
            *p++ = Base64URLTable[((string[i + 1] & 0xF) << 2)];
        }
    }

    *p++ = '\0';
}

void InitCache() {
    struct stat st = {0};
    
    if (stat("./cache", &st) == -1) {
        if (mkdir("./cache", 0755) != 0) {
            perror("Failed to create cache directory");
            exit(EXIT_FAILURE);
        }
    }
}

#ifdef _WIN32
HANDLE CreateCacheLock() {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    int tries = 100;

    while (tries--) {
        hFile = CreateFile(
            "./cache/.lockfile", GENERIC_READ | GENERIC_WRITE, 0, NULL,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL
        );

        if (hFile != INVALID_HANDLE_VALUE) {
            return hFile;
        }

        if (GetLastError() == ERROR_FILE_EXISTS) {
            sleep_ms(10);
        } else {
            break;
        }
    }

    return hFile;
}

void ReleaseCacheLock(HANDLE hFile) {
    CloseHandle(hFile);
    DeleteFile("./cache/.lockfile");
}
#else
int CreateCacheLock() {
    int fd = -1;
    int tries = 100;

    while (tries--) {
        fd = open("./cache/.lockfile", O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd != -1) return fd;
        sleep_ms(10);
    }

    return fd;
}

void ReleaseCacheLock(int fd){
    close(fd);
    unlink("./cache/.lockfile");
}

#endif

void CachePacked(const char *path, const unsigned char *out, const size_t outsize, const unsigned char *content, const size_t insize){
    char cache_path[1024];

    {
      char in_archive_path64[971];
      char content_hash64[45];
      char content_hash[32];

      // Construct the full file path: "{CWD}/cache/{content_hash64}-{in_archive_path64}"
      lonesha256(content_hash, content, insize);
      Base64URLEncode(content_hash64, content_hash, sizeof(content_hash));
      Base64URLEncode(in_archive_path64, path, strlen(path));
      snprintf(cache_path, sizeof(cache_path), "./cache/%s-%s", content_hash64, in_archive_path64);
    }

#ifdef _WIN32
    HANDLE lock = CreateCacheLock();
    if (lock == INVALID_HANDLE_VALUE){
#else
    int lock = CreateCacheLock();
    if (lock == -1){
#endif
        perror("Failed to update cache file");
        return;
    }

    // Open the file for writing (create if not exists, overwrite if exists)
    FILE *file = fopen(cache_path, "wb");
    if(!file) {
        perror("Failed to update cache file");
        ReleaseCacheLock(lock);
        return;
    }

    int success = 0;
    if(fwrite(out, sizeof(unsigned char), outsize, file) == outsize) {
        success = 1;
    }
    fclose(file);

    if(success == 0) {
        remove(cache_path);
        perror("Failed to update cache file");
    }
    ReleaseCacheLock(lock);
}

int ReadCache(char *path, const size_t insize, const unsigned char *content, unsigned char *out, size_t *outsize, uint32_t *flags){
    char cache_path[1024];

    {
      char in_archive_path64[971];
      char content_hash64[45];
      char content_hash[32];

      // Construct the full file path: "{CWD}/cache/{content_hash64}-{in_archive_path64}"
      lonesha256(content_hash, content, insize);
      Base64URLEncode(content_hash64, content_hash, sizeof(content_hash));
      Base64URLEncode(in_archive_path64, path, strlen(path));
      snprintf(cache_path, sizeof(cache_path), "./cache/%s-%s", content_hash64, in_archive_path64);
    }

#ifdef _WIN32
    HANDLE lock = CreateCacheLock();
    if (lock == INVALID_HANDLE_VALUE){
#else
    int lock = CreateCacheLock();
    if (lock == -1){
#endif
        perror("Cache is locked (./cache/.lockfile exists)");
        return 0;
    }

    // For reading and writing, only if it exists.
    FILE *file = fopen(cache_path, "rb+");
    if (!file) {
        ReleaseCacheLock(lock);
        return 0;
    }

    fseek(file, 0, SEEK_END);
    *outsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    fread(out, sizeof(unsigned char), *outsize, file);
    fclose(file);

    // TODO:
    // Validate file content, but collisions are unlikely

    // Match flags set by PackFile
    *flags = FLAG_FILE_COMPRESSED; 

    // Bump atime, mtime
    utime(cache_path, NULL);

    ReleaseCacheLock(lock);

    return 1;
}

void PackFiles(void *arguments){
    int threadId = *(int*)arguments;
    char **path;
    //size_t status;
    while((path = pop(&globals.work_queue, NULL)) != NULL){
        //printf("@%d [%d/%d] Starting %s...\n", threadId, status, globals.work_queue.size, *path);
        
        size_t insize;
        size_t outsize = 0;
        char *content = ExtractFile(globals.inMpq.mpq, &globals.inMpq.hd, &globals.inMpq.tbl, *path, &insize);
        if(!content)
            exit(1);
        size_t sotSize = 4*(1+ ceil( ((float)insize)/globals.blockSize ));
        unsigned char *out = malloc(insize + sotSize);
        uint32_t flags;
        int foundCache = 0;

        if(globals.useCache) {
            foundCache = ReadCache(*path, (const size_t)insize, (const unsigned char*)content, out, &outsize, &flags);
        }
        if(foundCache == 0){
            PackFile((unsigned char*)content, insize, insize + sotSize, out, &outsize, &flags);
            if (globals.useCache){
                CachePacked(*path, out, outsize, content, insize);
            }
        }

        Sys_Lock(globals.lock);

        fwrite(out, outsize, 1, globals.mpq_file);
        
        btentry_t bte;
        bte.filePos = globals.bytesWritten;
        bte.compressedSize = outsize;
        bte.normalSize = insize;
        bte.flags = flags | FLAG_FILE_EXISTS;

        ConvertSlashes(*path);
        Insert(&globals.mpq_table, *path, &bte);
        globals.bytesWritten += outsize;

        globals.totalInSize += insize;
        globals.totalOutSize += outsize;
        
        size_t status = globals.filesProceeded++;
        
        printf("@%d [%d/%d] Finished %s (%f)\n", threadId, status, globals.work_queue.size, *path, (float)outsize/insize);
        
        Sys_Unlock(globals.lock);
        
        free(content);
        free(out);
    }

}



void mkmpq(int num_threads){
    fseek(globals.mpq_file, 0x20, SEEK_CUR);
    
    globals.bytesWritten = 0x20;
    globals.totalInSize = 0;
    globals.totalOutSize = 0;
    
    
    sys_thread_t *threads;
    int *thread_args = malloc(num_threads*sizeof(int));

    if(num_threads > 1){
        threads = malloc((num_threads-1)*sizeof(void*));
        
        for(int i = 0; i != num_threads -1; i++){
            thread_args[i] = i;
            threads[i] = Sys_CreateThread(PackFiles, &thread_args[i]);
        }
    }

    thread_args[num_threads-1] = num_threads-1;
    PackFiles((void *)&thread_args[num_threads-1]);
    
    if(num_threads > 1){
        for (int i=0; i != num_threads -1; i++) {
            Sys_JoinThread(threads[i]);
        }
    }

    WriteHT();
    WriteBT();
    WriteHeader();
    
    
    fflush(globals.mpq_file);
    fclose(globals.mpq_file);

    printf("in: %d  out: %d\n", globals.totalInSize, globals.totalOutSize);
}

void CopyPreMPQData(){
    fwrite(globals.inMpq.file, globals.inMpq.offset, 1, globals.mpq_file);
}

void ReadInMpq(const char *path){
    globals.inMpq.file = Sys_ReadFile(path, &globals.inMpq.size);
    globals.inMpq.offset = FindHeader( globals.inMpq.file
                                     , globals.inMpq.size
                                     , &globals.inMpq.hd );
    if(globals.inMpq.offset == (uint32_t)(-1)){
        fprintf(stderr, "%s doesn't seem to be a mpq file\n", path);
        exit(1);
    }
    globals.inMpq.mpq = globals.inMpq.file + globals.inMpq.offset;
    
    DecryptBlock( globals.inMpq.mpq + globals.inMpq.hd.htPos
                , sizeof(htentry_t) * globals.inMpq.hd.htSize
                , hash("(hash table)", TableKey) );
    DecryptBlock( globals.inMpq.mpq + globals.inMpq.hd.btPos
                , sizeof(btentry_t) * globals.inMpq.hd.btSize
                , hash("(block table)", TableKey));
    
    globals.inMpq.tbl.htSize = globals.inMpq.hd.htSize;
    globals.inMpq.tbl.btSize = globals.inMpq.hd.btSize;
    globals.inMpq.tbl.ht = (htentry_t*)(globals.inMpq.mpq + globals.inMpq.hd.htPos);
    globals.inMpq.tbl.bt = (btentry_t*)(globals.inMpq.mpq + globals.inMpq.hd.btPos);
}

void PopulateListfile(const char *path){
    size_t listfile_size;
    char *listfile;
    if(path){
        listfile = Sys_ReadFile(path, &listfile_size);
        ReadListfile(&globals.listfile, &globals.inMpq.tbl, listfile, listfile_size);
    }
    
    listfile = ExtractFile(globals.inMpq.mpq, &globals.inMpq.hd, &globals.inMpq.tbl, "(listfile)", &listfile_size);
    if(listfile){
        printf("Found internal listfile.\n");
        ReadListfile(&globals.listfile, &globals.inMpq.tbl, listfile, listfile_size);
    }

    
    char *internalNames = calloc(1, 24);
    memcpy(internalNames, "(listfile);(attributes)", 24);
    ReadListfile(&globals.listfile, &globals.inMpq.tbl, internalNames, strlen(internalNames));

    // for some bloody reason it doesnt work with strdup on windows/msys/cygwin
    // so we use the above way
    // char *internalNames = strdup("(listfile);(attributes)");
    //ReadListfile(&globals.listfile, &globals.inMpq.tbl, internalNames, strlen(internalNames));
    // we can't free internalNames here because we compare the extracted names later
    //free(internalNames);
}

void PrintHelp(char *name){
    printf("Usage: %s [--threads | -t THREADS] [--iterations | -i ITERATIONS] [--listfile | -l listfile] [--shift-size | -s shiftsize] [--block-splitting-max iterations] in-file out-file\n", name);
    printf("  in-file:                The input mpq\n");
    printf("  out-file:               The compressed mpq\n");
    printf("  --threads,  -t:         How many threads are started. Default: 2.\n");
    printf("  --iterations, -i:       How many iterations are spent on compressing every file. Default 15.\n");
    printf("  --shift-size, -s:       Sets the Blocksize to 512*2^shiftsize. Default shiftsize: 15\n");
    printf("  --block-splitting-max:  Maximum amount of blocks to split into (0 for unlimited, but this can give\n"
           "                          extreme results that hurt compression on some files). Default value: 15.\n");
    printf("  --cache, -c:            Use an in-disk cache to speed up later executions.\n");
    printf("  --help, -h:             Prints this help.\n");
}

int main(int argc, char **argv){
    size_t threads = 2;
    int shift = 15;
    int cache = 0;
    char *external_listfile_path = NULL, *inmpq_path, *outmpq_path;
    
    globals.zopfli_options.verbose = 0;
    globals.zopfli_options.verbose_more = 0;
    globals.zopfli_options.numiterations = 15;
    globals.zopfli_options.blocksplitting = 1;
    globals.zopfli_options.blocksplittingmax  = 15;
    
    globals.filesProceeded = 1;
    
    
    int arg = 1;
    for(;arg != argc; arg++){
        if(!strcmp("--threads", argv[arg]) || !strcmp("-t", argv[arg])){
            arg++;
            if(arg >= argc){
                printf("--threads, -t requires one more argument.\n");
                exit(0);
            }
            threads = atoi(argv[arg]);
            if(threads <= 0){
                printf("The number of threads must be greater than 0\n");
                exit(0);
            }
        } else if(!strcmp("--iterations", argv[arg]) || !strcmp("-i", argv[arg])){
            arg++;
            if(arg >= argc){
                printf("--iterations, -i requires one more argument.\n");
                exit(0);
            }
            globals.zopfli_options.numiterations = atoi(argv[arg]);
            if(globals.zopfli_options.numiterations <= 0){
                printf("The number of iterations must be greater than 0\n");
                exit(0);
            }
        } else if(!strcmp("--listfile", argv[arg]) || !strcmp("-l", argv[arg])){
            arg++;
            if(arg >= argc){
                printf("--listfile, -l requires one more argument.\n");
                exit(0);
            }
            external_listfile_path = argv[arg];
        } else if(!strcmp("--block-splitting-max", argv[arg])){
            arg++;
            if(arg >= argc){
                printf("--block-splitting-max requires one more argument.\n");
                exit(0);
            }
            globals.zopfli_options.blocksplittingmax = atoi(argv[arg]);
            if(globals.zopfli_options.blocksplittingmax <= 0){
                printf("The number of block must be greater than 0\n");
                exit(0);
            }
        } else if(!strcmp("--shift-size", argv[arg]) || !strcmp("-s", argv[arg])){
            arg++;
            if(arg >= argc){
                printf("--shift-size, -s requires one more argument.\n");
                exit(0);
            }
            shift = atoi(argv[arg]);
            
            if(shift < 0 || shift > 15){
                printf("The number of block must be betwee 1 and 15\n");
                exit(0);
            }
        } else if (!strcmp("--cache", argv[arg]) || !strcmp("-c", argv[arg])){
            cache = 1;
        } else if (!strcmp("--help", argv[arg]) || !strcmp("-h", argv[arg])){
            PrintHelp(argv[0]);
            exit(0);
        } else {
            break;
        }
    }
    if(argc - arg < 2){
        PrintHelp(argv[0]);
        exit(0);
    }
    inmpq_path = argv[arg];
    outmpq_path = argv[arg+1];
    
    
    globals.mpqShift = (size_t)shift;
    globals.blockSize = 512 * (1 << globals.mpqShift);
    globals.useCache = cache;
    
    globals.mpq_file = fopen(outmpq_path, "wb");
    if(globals.mpq_file == NULL){
        fprintf(stderr, "Couldn't open file '%s'\n", outmpq_path);
        exit(1);
    }
    if (cache == 1){
        InitCache();
    }

    PrepareCryptTable();
    ReadInMpq(inmpq_path);

    InitListfile(&globals.listfile, globals.inMpq.tbl.htSize);
    PopulateListfile(external_listfile_path);

    if(!ListfileSufficient(&globals.inMpq.tbl, &globals.listfile)){
        fprintf(stderr, "Insufficient listfile.\n");
        exit(0);
    }

    
    InitTable(&globals.mpq_table, globals.inMpq.tbl.btSize);
    
    char **pathes = malloc(sizeof(char*)*globals.inMpq.tbl.btSize);
    size_t cnt = 0;
    for(size_t i = 0; i != globals.listfile.size; i++){
        if(globals.listfile.list[i].hash != 0){
            char *path = globals.listfile.list[i].path;
            if(!strcmp("(listfile)", path) || !strcmp("(attributes)", path))
                continue;
            pathes[cnt++] = path;
        }
    }

    InitQueue(&globals.work_queue, pathes, cnt, sizeof(char*));
    
    globals.lock = Sys_CreateLock();
    
    CopyPreMPQData();
    mkmpq(threads);
}
