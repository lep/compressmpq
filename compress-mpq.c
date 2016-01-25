#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "zopfli/zopfli.h"
#include "miniz.c"

#include "thread.h"
#include "table.h"
#include "crypto.h"
#include "queue.h"
#include "listfile.h"

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
    mz_ulong destLen;

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
        if(*fileInMpq != 2){
            // we don't handle anything but zlib compression
            fprintf(stderr, "Cannot decompress non-zlib compressed file\n");
            free(file);
            return NULL;
        }
        destLen = bte->normalSize;
        mz_uncompress(file, &destLen, fileInMpq+1, (mz_ulong)(bte->compressedSize-1));
        
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
            destLen = thisSectorSize;
            
            if(encrypted)
                DecryptBlock(fileInMpq+sectorOffsetTable[idx], size, baseKey+idx);
            
            if(size == thisSectorSize){
                // this sector is not compressed
                memcpy(file+offset, fileInMpq+sectorOffsetTable[idx], size);
            }else{
                if(fileInMpq[sectorOffsetTable[idx]] != 2){
                    fprintf(stderr, "Cannot decompress non-zlib compressed file (%d)\n", *(fileInMpq+sectorOffsetTable[idx]));
                    free(file);
                    return NULL;
                }
                mz_uncompress(file+offset, &destLen, fileInMpq+sectorOffsetTable[idx]+1, (mz_ulong)(size-1));
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


void PackFile(char *path, unsigned char *content, size_t contentSize, size_t bufferSize, unsigned char *out, size_t *outsize, uint32_t *flags){
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

        PackFile(*path, (unsigned char*)content, insize, insize + sotSize, out, &outsize, &flags);

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
        
        Sys_Unlock(globals.lock);
        
        
        free(content);
        free(out);
        printf("@%d [%d/%d] Finished %s (%f)\n", threadId, status, globals.work_queue.size, *path, (float)outsize/insize);
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
    
    char *internalNames = "(listfile);(attributes)";
    ReadListfile(&globals.listfile, &globals.inMpq.tbl, internalNames, strlen(internalNames));
}

void PrintHelp(char *name){
    printf("Usage: %s [--threads | -t THREADS] [--iterations | -i ITERATIONS] [--listfile | -l listfile] [--shift-size | -s shiftsize] [--block-splitting-max iterations] [--block-splitting-last] in-file out-file\n", name);
    printf("  in-file:                The input mpq\n");
    printf("  out-file:               The compressed mpq\n");
    printf("  --threads,  -t:         How many threads are started. Default: 2.\n");
    printf("  --iterations, -i:       How many iterations are spent on compressing every file. Default 15.\n");
    printf("  --shift-size, -s:       Sets the Blocksize to 512*2^shiftsize. Default shiftsize: 15\n");
    printf("  --block-splitting-last: If true, chooses the optimal block split points only after doing the iterative\n" 
           "                          LZ77 compression. If false, chooses the block split points first, then does\n"
           "                          iterative LZ77 on each individual block. Depending on the file, either first\n"
           "                          or last gives the best compression. Default: false \n");
    printf("  --block-splitting-max:  Maximum amount of blocks to split into (0 for unlimited, but this can give\n"
           "                          extreme results that hurt compression on some files). Default value: 15.\n");
    printf("  --help, -h:             Prints this help.\n");
}

int main(int argc, char **argv){
    size_t threads = 2;
    int shift = 15;
    char *external_listfile_path = NULL, *inmpq_path, *outmpq_path;
    
    globals.zopfli_options.verbose = 0;
    globals.zopfli_options.verbose_more = 0;
    globals.zopfli_options.numiterations = 15;
    globals.zopfli_options.blocksplitting = 1;
    globals.zopfli_options.blocksplittinglast = 0;
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
            
        } else if(!strcmp("--block-splitting-last", argv[arg])){
            arg++;
            globals.zopfli_options.blocksplittinglast = 1;
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
    
    globals.mpq_file = fopen(outmpq_path, "wb");
    if(globals.mpq_file == NULL){
        fprintf(stderr, "Couldn't open file '%s'\n", outmpq_path);
        exit(1);
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
