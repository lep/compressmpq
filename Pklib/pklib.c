#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "explode.h"

/******************************************************************************/
/*                                                                            */
/*  Support functions for PKWARE Data Compression Library compression (0x08)  */
/*                                                                            */
/******************************************************************************/

// Local structures

// Information about the input and output buffers for pklib
typedef struct
{
    unsigned char * pbInBuff;           // Pointer to input data buffer
    unsigned char * pbInBuffEnd;        // End of the input buffer
    unsigned char * pbOutBuff;          // Pointer to output data buffer
    unsigned char * pbOutBuffEnd;       // Pointer to output data buffer
} TDataInfo;

// Function loads data from the input buffer. Used by Pklib's "implode"
// and "explode" function as user-defined callback
// Returns number of bytes loaded
//    
//   char * buf          - Pointer to a buffer where to store loaded data
//   unsigned int * size - Max. number of bytes to read
//   void * param        - Custom pointer, parameter of implode/explode

static unsigned int ReadInputData(char * buf, unsigned int * size, void * param)
{
    TDataInfo * pInfo = (TDataInfo *)param;
    unsigned int nMaxAvail = (unsigned int)(pInfo->pbInBuffEnd - pInfo->pbInBuff);
    unsigned int nToRead = *size;

    // Check the case when not enough data available
    if(nToRead > nMaxAvail)
        nToRead = nMaxAvail;
    
    // Load data and increment offsets
    memcpy(buf, pInfo->pbInBuff, nToRead);
    pInfo->pbInBuff += nToRead;
    assert(pInfo->pbInBuff <= pInfo->pbInBuffEnd);
    return nToRead;
}

// Function for store output data. Used by Pklib's "implode" and "explode"
// as user-defined callback
//    
//   char * buf          - Pointer to data to be written
//   unsigned int * size - Number of bytes to write
//   void * param        - Custom pointer, parameter of implode/explode

static void WriteOutputData(char * buf, unsigned int * size, void * param)
{
    TDataInfo * pInfo = (TDataInfo *)param;
    unsigned int nMaxWrite = (unsigned int)(pInfo->pbOutBuffEnd - pInfo->pbOutBuff);
    unsigned int nToWrite = *size;

    // Check the case when not enough space in the output buffer
    if(nToWrite > nMaxWrite)
        nToWrite = nMaxWrite;

    // Write output data and increments offsets
    memcpy(pInfo->pbOutBuff, buf, nToWrite);
    pInfo->pbOutBuff += nToWrite;
    assert(pInfo->pbOutBuff <= pInfo->pbOutBuffEnd);
}


int DecompressPKLIB(void * pvOutBuffer, int * pcbOutBuffer, void * pvInBuffer, int cbInBuffer)
{
    TDataInfo Info;                             // Data information
    char * work_buf = malloc(EXP_BUFFER_SIZE*sizeof(char));// Pklib's work buffer
    //STORM_ALLOC(char, EXP_BUFFER_SIZE);// Pklib's work buffer


    // Handle no-memory condition
    if(work_buf == NULL){
        return 0;
    }

    // Fill data information structure
    memset(work_buf, 0, EXP_BUFFER_SIZE);
    Info.pbInBuff     = (unsigned char *)pvInBuffer;
    Info.pbInBuffEnd  = (unsigned char *)pvInBuffer + cbInBuffer;
    Info.pbOutBuff    = (unsigned char *)pvOutBuffer;
    Info.pbOutBuffEnd = (unsigned char *)pvOutBuffer + *pcbOutBuffer;

    // Do the decompression
    explode(ReadInputData, WriteOutputData, work_buf, &Info);
    
    // If PKLIB is unable to decompress the data, return 0;
    if(Info.pbOutBuff == pvOutBuffer)
    {
        free(work_buf);
        //STORM_FREE(work_buf);
        return 0;
    }

    // Give away the number of decompressed bytes
    *pcbOutBuffer = (int)(Info.pbOutBuff - (unsigned char *)pvOutBuffer);
    //STORM_FREE(work_buf);
    free(work_buf);
    return 1;
}
