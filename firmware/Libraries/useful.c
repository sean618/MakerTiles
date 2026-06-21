/* Copyright (c) 2024 Sean Bremner */

// #include <stdio.h>
#include <stdlib.h>
// #include <math.h>
// #include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "useful.h"
#include "project.h"

// void modifyBitsU16(uint16_t * regData, uint16_t data, uint8_t startBit, uint8_t numBits) {
//     uint8_t offset = startBit;
//     uint16_t bitmask = (1 << numBits) - 1;
//     myAssert(data <= bitmask, "Data exceeds greater than field size");
//     *regData = (*regData & ~(bitmask << offset)) | ((data & bitmask) << offset);
// }

// void modifyBitsU8(uint8_t * regData, uint8_t data, uint8_t startBit, uint8_t endBit) {
//     uint8_t numBits = (endBit - startBit) + 1;
//     uint8_t offset = startBit;
//     uint8_t bitmask = (1 << numBits) - 1;
//     myAssert(data <= bitmask, "Data exceeds greater than field size");
//     *regData = (*regData & ~(bitmask << offset)) | ((data & bitmask) << offset);
// }

// uint16_t getBitsU16(uint16_t data, uint8_t startBit, uint8_t numBits) {
//     uint8_t offset = startBit;
//     uint16_t bitmask = (1 << numBits) - 1;
//     return ((data >> offset) & bitmask);
// }

// uint8_t getBitsU8(uint8_t data, uint8_t startBit, uint8_t endBit) {
//     uint8_t numBits = (endBit - startBit) + 1;
//     uint8_t offset = startBit;
//     uint8_t bitmask = (1 << numBits) - 1;
//     return ((data >> offset) & bitmask);
// }
// uint8_t getBitsU8LE(uint8_t data, uint8_t startBit, uint8_t endBit) {
//     uint8_t numBits = (endBit - startBit) + 1;
//     uint8_t bitmask = (1 << numBits) - 1;
//     uint8_t offset = 7 - endBit;
//     return ((data >> offset) & bitmask);
// }

// uint32_t power(uint32_t num, uint8_t pow) {
//     uint32_t result = 1;
//     for (uint8_t i=0; i<pow; i++)
//         result *= num;
//     return result;
// }

// uint32_t getRandomInt(uint32_t max) {
//     static uint64_t g_seed; // TODO - set this properly
//     static uint8_t initialized = 0;
//     if (!initialized) { // This is not thread safe, but a race condition here is not harmful.
//         initialized = 1;
//         g_seed = time(NULL);
//     }
//     g_seed = (214013*g_seed+2531011); 
// 	uint64_t tmp = (g_seed>>16) & 0x7FFF;
// 	uint64_t tmp2 = (max * tmp) / 0x8000;
//     return tmp2;

//     // // TODO: perhaps put back. At the moment though it uses 4KB of flash because it seems to be calling printf stuff
//     // static uint8_t initialized = 0;
//     // if (!initialized) { // This is not thread safe, but a race condition here is not harmful.
//     //     initialized = 1;
//     //     srand((uint32_t) time(NULL));
//     // }
//     // // Plus RAND_MAX/2 so that it rounds (rather than always rounding down)
//     // // RAND_MAX +1 is optimisation - it should mean the divide becomes a bit shift
//     // return (((rand() * max) + (RAND_MAX/2)) / ((uint32_t) RAND_MAX + 1));
// }

// /*Integer square root - Obtained from Stack Overflow (14/6/15):
//  * http://stackoverflow.com/questions/1100090/looking-for-an-efficient-integer-square-root-algorithm-for-arm-thumb2
//  * User: Gutskalk
//  */
// uint16_t isqrt(uint32_t x)
// {
//     uint16_t res=0;
//     uint16_t add= 0x8000;
//     int i;
//     for(i=0;i<16;i++)
//     {
//         uint16_t temp=res | add;
//         uint32_t g2=temp*temp;
//         if (x>=g2)
//         {
//             res=temp;
//         }
//         add>>=1;
//     }
//     return res;
// }



// uint8_t log2RoundedDown(uint32_t value) {
//     for (uint8_t i=31; i>0; i--) {
//         if (value >> i) {
//             return i;
//         }
//     }
//     return 0;
// }

// // Fast arctan2 - found in forum
// // TODO: haven't tested yet
// uint16_t arctan2(int16_t x, int16_t y) {
//     if (x == 0 && y == 0) {
//         return 0;
//     }
//     uint16_t angleDeg;
//     int32_t r;
//     int16_t coeff1 = 180 / 4;
//     int16_t coeff2 = 3 * coeff1;
//     int16_t yAbs = y;
//     if (yAbs < 0)
//         yAbs = -yAbs;
//     if(x >= 0) {
//        r = ((x - yAbs) * coeff1) / (x + yAbs);
//        angleDeg = coeff1 - r;
//     } else {
//        r = ((x + yAbs) * coeff1) / (yAbs - x);
//        angleDeg = coeff2 - r;
//     }
//     if (y < 0)
//         return(-angleDeg);     // negate if in quad III or IV
//     else
//         return(angleDeg);
// }

// // x between 0-1024 represents 0-360 degrees
// int16_t fastSin16bit(uint16_t x) {
//     // Uses 512B of RAM
//     static int16_t sinWave[257] = {
//             0,   201,   402,   603,   804,  1005,  1206,  1406, 
//          1607,  1808,  2009,  2209,  2410,  2610,  2811,  3011, 
//          3211,  3411,  3611,  3811,  4011,  4210,  4409,  4608, 
//          4807,  5006,  5205,  5403,  5601,  5799,  5997,  6195, 
//          6392,  6589,  6786,  6982,  7179,  7375,  7571,  7766, 
//          7961,  8156,  8351,  8545,  8739,  8932,  9126,  9319, 
//          9511,  9703,  9895, 10087, 10278, 10469, 10659, 10849, 
//         11038, 11227, 11416, 11604, 11792, 11980, 12166, 12353, 
//         12539, 12724, 12909, 13094, 13278, 13462, 13645, 13827, 
//         14009, 14191, 14372, 14552, 14732, 14911, 15090, 15268, 
//         15446, 15623, 15799, 15975, 16150, 16325, 16499, 16672, 
//         16845, 17017, 17189, 17360, 17530, 17699, 17868, 18036, 
//         18204, 18371, 18537, 18702, 18867, 19031, 19194, 19357, 
//         19519, 19680, 19840, 20000, 20159, 20317, 20474, 20631, 
//         20787, 20942, 21096, 21249, 21402, 21554, 21705, 21855, 
//         22004, 22153, 22301, 22448, 22594, 22739, 22883, 23027, 
//         23169, 23311, 23452, 23592, 23731, 23869, 24006, 24143, 
//         24278, 24413, 24546, 24679, 24811, 24942, 25072, 25201, 
//         25329, 25456, 25582, 25707, 25831, 25954, 26077, 26198, 
//         26318, 26437, 26556, 26673, 26789, 26905, 27019, 27132, 
//         27244, 27355, 27466, 27575, 27683, 27790, 27896, 28001, 
//         28105, 28208, 28309, 28410, 28510, 28608, 28706, 28802, 
//         28897, 28992, 29085, 29177, 29268, 29358, 29446, 29534, 
//         29621, 29706, 29790, 29873, 29955, 30036, 30116, 30195, 
//         30272, 30349, 30424, 30498, 30571, 30643, 30713, 30783, 
//         30851, 30918, 30984, 31049, 31113, 31175, 31236, 31297, 
//         31356, 31413, 31470, 31525, 31580, 31633, 31684, 31735, 
//         31785, 31833, 31880, 31926, 31970, 32014, 32056, 32097, 
//         32137, 32176, 32213, 32249, 32284, 32318, 32350, 32382, 
//         32412, 32441, 32468, 32495, 32520, 32544, 32567, 32588, 
//         32609, 32628, 32646, 32662, 32678, 32692, 32705, 32717, 
//         32727, 32736, 32744, 32751, 32757, 32761, 32764, 32766,
//         32767
//     };
//     uint8_t section = x / 256;
//     section = section % 4;
//     uint16_t index = x % 256;
//     if (section == 1 || section == 3)
//         index = (256 - index);
//     int16_t sinVal = sinWave[index];
//     if (section == 2 || section == 3)
//         sinVal = -sinVal;
//     return sinVal;
// }

// int8_t fastSin8bit(uint8_t x) {
//     // To keep this fast make sure it stays in the heap - static
//     // It would be about 40% faster to make it just a look up but
//     // uses 200B more ram
//     static int8_t sinWave[64] = {
//           0,   3,   6,   9,  12,  15,  18,  21, 
//          24,  28,  31,  34,  37,  40,  43,  46, 
//          48,  51,  54,  57,  60,  63,  65,  68, 
//          71,  73,  76,  78,  81,  83,  85,  88, 
//          90,  92,  94,  96,  98, 100, 102, 104, 
//         106, 108, 109, 111, 112, 114, 115, 117, 
//         118, 119, 120, 121, 122, 123, 124, 124, 
//         125, 126, 126, 127, 127, 127, 127, 127,
//     };
//     uint8_t section = x / 64;
//     uint8_t index = x % 64;
//     if (section == 1 || section == 3)
//         index = (63 - index);
//     int16_t sinVal = sinWave[index];
//     if (section == 2 || section == 3)
//         sinVal = -sinVal;
//     return sinVal;
// }

// ==================================== //
// Generic thread safe circular buffer implementations
// append only touches the end pointer
// pop only touches the start pointer
// The buffer is empty if start == end

unsigned incrAndWrap(unsigned val, unsigned inc, unsigned max) {
    unsigned newVal = val + inc;
    return ((newVal >= max) ? (newVal - max) : (newVal));
}
int decrAndWrap(unsigned val, unsigned decr, unsigned max) {
    int newVal = val - decr;
    return ((newVal < 0) ? (newVal + max) : newVal);
}

uint16_t bufferLength(sCircularBuffer * buffer) {
    int32_t length = (int32_t) buffer->end - (int32_t) buffer->start;
    if (length < 0) {
        length += buffer->size;
    }
    return length;
    // if (buffer->end >= buffer->start) {
    //     return buffer->end - buffer->start;
    // } else {
    //     return buffer->end + buffer->size - buffer->start;
    // }
}

bool bufferHasSpace(sCircularBuffer * buffer, uint16_t numBytes) {
    unsigned newSize = bufferLength(buffer) + numBytes;
    return (newSize < buffer->size);
    // if (buffer->end >= buffer->start) {
    //     return ((buffer->end - buffer->start + numBytes) < buffer->size);
    // } else {
    //     return ((buffer->end + buffer->size - buffer->start + numBytes) < buffer->size);
    // }
}

// return of -1 means overflow
int8_t appendBufferWithTmpEnd(sCircularBuffer * buffer, uint16_t * tmpEnd, const uint8_t * bytes, uint16_t numBytes) {
    unsigned newSize = bufferLength(buffer) + numBytes;
    if (newSize >= buffer->size) {
        return -1;
    }
    buffer->maxUsage = MAX(buffer->maxUsage, newSize);

    // TO avoid any c integer assumptions/mistakes just put everything into a int32
    int32_t tmpNumBytes = numBytes;
    int32_t end = *tmpEnd; //buffer->end;
    int32_t start = buffer->start;
    int32_t size = buffer->size;

    softAssert(start < size, "");

    // Examples:
    // 00, 01, 02, 03, 04, 05, 06, 07, 08, 09
    // size = 10, start = 1, end = 6, bytes = 4
    // size = 32768, end = 32764, bytes = 5

    // END is not inclusive
    // so end = 0, numBytes = size, newEnd = 0


    int32_t newEnd = end + tmpNumBytes;
    int32_t length1 = tmpNumBytes;
    int32_t length2 = 0;
    if (newEnd >= size) {
        length1 = size - end;
        length2 = tmpNumBytes - length1;
        newEnd = length2;
    }
    softAssert(length1 >= 0, "");
    softAssert(length2 >= 0, "");
    softAssert(length1 <= tmpNumBytes, "");
    softAssert(length2 <= tmpNumBytes, "");
    softAssert((end + length1) <= size, "");
    softAssert(length2 < start || length2 == 0, "");
    softAssert(newEnd < size, "");

    memcpy(&buffer->data[end], bytes, length1);
    if (length2) {
        memcpy(&buffer->data[0], &bytes[length1], length2);
    }

    softAssert(start < size, "");

    // NOTE: this function must be thread safe - so only update the end once the data has been copied
    *tmpEnd = newEnd;
    
    // buffer->end = newEnd;
    return 0;
}

// return of -1 means overflow
int8_t appendBuffer(sCircularBuffer * buffer, const uint8_t * bytes, uint16_t numBytes) {
    return appendBufferWithTmpEnd(buffer, &buffer->end, bytes, numBytes);
}

// return of -1 means underflow
int8_t popBuffer(sCircularBuffer * buffer, uint8_t * bytes, uint16_t numBytes) {
    // TO avoid any c integer assumptions/mistakes just put everything into a int32
    int32_t tmpNumBytes = numBytes;
    int32_t start = buffer->start;
    int32_t size = buffer->size;

    if (bufferLength(buffer) < numBytes)
        return -1;

    softAssert(start < size, "");

    // Examples:
    // 00, 01, 02, 03, 04, 05, 06, 07, 08, 09
    // size = 10, start = 0, end = 1, bytes = 1 <-- read from 0
    // size = 10, start = 9, end = 5, bytes = 4 <-- read from 9, 0, 1, 2

    int32_t newStart = start + tmpNumBytes;
    int32_t length1 = tmpNumBytes;
    int32_t length2 = 0;
    if (newStart >= size) {
        length1 = size - start;
        length2 = tmpNumBytes - length1;
        newStart = length2;
    }
    softAssert(length1 >= 0, "");
    softAssert(length2 >= 0, "");
    softAssert(length1 <= tmpNumBytes, "");
    softAssert(length2 <= tmpNumBytes, "");
    softAssert(newStart < size, "");

    memcpy(bytes, &buffer->data[start], length1);
    if (length2) {
        memcpy(&bytes[length1], &buffer->data[0], length2);
    }
    // NOTE: this function must be thread safe - so only update the start once the data has been copied
    buffer->start = newStart;
    return 0;
}

// ==================================== //

