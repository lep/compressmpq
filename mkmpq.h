#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "zopfli/zopfli.h"

#include "crypto.h"
#include "queue.h"
#include "table.h"
#include "thread.h"

#define SHIFT_SIZE (15)
#define BLOCK_SIZE (512*(1 << SHIFT_SIZE))

#define COMPRESSED_FILE 0x00000200
#define SINGLE_FILE     0x01000000
#define FILE_EXISTS     0x80000000


struct {
    int zopfli_iterations;
    FILE *mpq_file;
    sys_lock_t lock;
    table mpq_table;
    queue_t work_queue;
    size_t bytesWritten;
    size_t totalInSize;
    size_t totalOutSize;
} globals;


static void writeInt(unsigned char *out, size_t off, uint32_t n){
    out[off++] = n & 0xFF;
    out[off++] = (n >> 8) & 0xFF;
    out[off++] = (n >> 16) & 0xFF;
    out[off++] = (n >> 24) & 0xFF;
}

static char* readFile(const char *path, size_t *insize){
    FILE *fh = fopen(path, "rb");
    if(fh == NULL){
        fprintf(stderr, "Couldn't open file '%s'\n", path);
        exit(1);
    }
    fseek(fh, 0, SEEK_END);
    long s = ftell(fh);
    rewind(fh);
    char *buffer = (char*)malloc(s);
    fread(buffer, s, 1, fh);
    fclose(fh);
    *insize = s;
    return buffer;
}

static void packFile(unsigned char *content, size_t insize, unsigned char *out, size_t *outsize, uint32_t *flags){
    unsigned char *zopfli_out;
    size_t zopfli_outsize = 0;
    ZopfliOptions options;
    options.verbose = 0;
    options.verbose_more = 0;
    options.numiterations = globals.zopfli_iterations;
    options.blocksplitting = 1;
    options.blocksplittinglast = 0;
    options.blocksplittingmax  = 15;
    ZopfliFormat format = ZOPFLI_FORMAT_ZLIB;
    
    ZopfliCompress(&options, format, content, insize, &zopfli_out, &zopfli_outsize);
    
    if(zopfli_outsize < insize && zopfli_outsize <= BLOCK_SIZE -2){
        *flags = SINGLE_FILE | COMPRESSED_FILE;
        *outsize = zopfli_outsize;
        out[0] = 2;
        memcpy(out+1, zopfli_out, zopfli_outsize);
        free(zopfli_out);
    }else if(zopfli_outsize > insize && insize <= BLOCK_SIZE){
        *flags = SINGLE_FILE;
        memcpy(out, content, insize);
        *outsize = insize;
        free(zopfli_out);
    }else{
        size_t saved = 0, written = 0;
        size_t offsetTableSize = 4*(1+ ceil( ((float)insize)/BLOCK_SIZE ));
        size_t *sectorOffsetTable = (size_t*)malloc(offsetTableSize);
        size_t tableIdx = 1;
        size_t outpos = 0;
        
        for(size_t start = 0, end = insize; start <= end; start += BLOCK_SIZE){
            size_t len = end-start > BLOCK_SIZE ? BLOCK_SIZE : end-start;
            zopfli_outsize = 0;
            ZopfliCompress(&options, format, content+start, len, &zopfli_out, &zopfli_outsize);
            if(zopfli_outsize < len && zopfli_outsize <= BLOCK_SIZE -2){
                saved += len -zopfli_outsize-1;
                written += 1+zopfli_outsize;
                out[outpos] = 2;
                memcpy(out+outpos+1, zopfli_out, zopfli_outsize);
                sectorOffsetTable[tableIdx++] = 1+zopfli_outsize;
                outpos += 1+zopfli_outsize;
                
            }else{
                written += len;
                memcpy(out+outpos, content+start, len);
                sectorOffsetTable[tableIdx++] = len;
                outpos += len;
            }
            free(zopfli_out);
        }
        
        
        if(saved <= offsetTableSize){
            memcpy(out, content, insize);
            *outsize = insize;
            *flags = 0;
        }else{
            memmove(out+offsetTableSize, out, written);
            *outsize = written + offsetTableSize;
            *flags = COMPRESSED_FILE;
            sectorOffsetTable[0] = offsetTableSize;
            for(size_t i = 0, acc = 0; i != tableIdx; i++){
                acc += sectorOffsetTable[i];
                writeInt(out, i*4, acc);
            }

        }
    }
}

static void convertSlashes(char *path){
    for(int i = 0; path[i]; i++){
        if(path[i]=='/')
            path[i]='\\';
    }
}

void packFiles(void *arguments){
    int threadId = *(int*)arguments;
    char **path;
    int status;
    while((path = pop(&globals.work_queue, &status)) != NULL){
        printf("@%d [%d/%d] Starting %s...\n", threadId, status, globals.work_queue.size, *path);
        
        size_t insize;
        size_t outsize = 0;
        char *content = readFile(*path, &insize);
        unsigned char *out = (unsigned char*)malloc(insize);
        uint32_t flags;

        packFile((unsigned char*)content, insize, out, &outsize, &flags);

        Sys_Lock(globals.lock);

        fwrite(out, outsize, 1, globals.mpq_file);
        
        btentry bte;
        bte.filePos = globals.bytesWritten;
        bte.compressedSize = outsize;
        bte.normalSize = insize;
        bte.flags = flags | FILE_EXISTS;

        convertSlashes(*path);
        insert(&globals.mpq_table, *path, &bte);
        globals.bytesWritten += outsize;

        globals.totalInSize += insize;
        globals.totalOutSize += outsize;
        
        Sys_Sys_Sys_Unlock(globals.lock);
        
        
        free(content);
        free(out);

        printf("@%d [%d/%d] Finished %s (%f)!\n", threadId, status, globals.work_queue.size, *path, (float)outsize/insize);
    }

}


static void writeHT(){
    if(globals.mpq_table.htSize == 0)
        return;
    unsigned char *ht = (unsigned char*)malloc(16*globals.mpq_table.htSize);
    for(size_t i = 0; i != globals.mpq_table.htSize; i++){
        size_t idx = i*16;
        writeInt(ht, idx, globals.mpq_table.ht[i].hashA);
        writeInt(ht, idx+4, globals.mpq_table.ht[i].hashB);
        writeInt(ht, idx+8, 0);
        writeInt(ht, idx+12, globals.mpq_table.ht[i].blockIndex);
    }
    encryptBlock(ht, 16*globals.mpq_table.htSize, hash("(hash table)", TableKey));
    fwrite(ht, 16*globals.mpq_table.htSize, 1, globals.mpq_file);
}

static void writeBT(){
    if(globals.mpq_table.btSize == 0)
        return;
    unsigned char *bt = (unsigned char*)malloc(16*(globals.mpq_table.btSize));
    for(size_t i = 0; i != globals.mpq_table.btSize; i++){
        size_t idx = i*16;
        writeInt(bt, idx, globals.mpq_table.bt[i].filePos);
        writeInt(bt, idx+4, globals.mpq_table.bt[i].compressedSize);
        writeInt(bt, idx+8, globals.mpq_table.bt[i].normalSize);
        writeInt(bt, idx+12, globals.mpq_table.bt[i].flags);
    }
    encryptBlock(bt, 16*(globals.mpq_table.btSize), hash("(block table)", TableKey));
    fwrite(bt, 16*(globals.mpq_table.btSize), 1, globals.mpq_file);
}

static void writeHeader(){
    uint32_t htSize = globals.mpq_table.htSize * 16;
    uint32_t btSize = globals.mpq_table.btSize * 16;
    uint32_t archiveSize = globals.bytesWritten + htSize + btSize;
    uint32_t htPos = globals.bytesWritten;
    uint32_t btPos = globals.bytesWritten + htSize;

    unsigned char buffer[4];
    
    fseek(globals.mpq_file, 0, SEEK_SET);
    
    fwrite("MPQ\x1a", 4, 1, globals.mpq_file);
    fwrite("\x20\0\0\0", 4, 1, globals.mpq_file);
    
    writeInt(buffer, 0, archiveSize);
    fwrite(buffer, 4, 1, globals.mpq_file);
    fwrite("\0\0", 2, 1, globals.mpq_file);

    unsigned char shift = SHIFT_SIZE;
    fwrite(&shift, 1, 1, globals.mpq_file);
    fwrite("\0", 1, 1, globals.mpq_file);
    
    writeInt(buffer, 0, htPos);
    fwrite(buffer, 4, 1, globals.mpq_file);
    
    writeInt(buffer, 0, btPos);
    fwrite(buffer, 4, 1, globals.mpq_file);
    
    writeInt(buffer, 0, globals.mpq_table.htSize);
    fwrite(buffer, 4, 1, globals.mpq_file);
    
    writeInt(buffer, 0, globals.mpq_table.btSize);
    fwrite(buffer, 4, 1, globals.mpq_file);
}