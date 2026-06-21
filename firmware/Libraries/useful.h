/* Copyright (c) 2024 Sean Bremner */

#ifndef USEFUL_H
#define USEFUL_H

// #include <string.h>
#include <stdbool.h>
#include <stdint.h>

// The QUEUE_*/BUFFER_* macros below expand to softAssert (defined in myAssert.h).
#include "myAssert.h"

// #define AF_SUCCESS 0
// #define AF_FAILURE 1

typedef enum {
    AF_SUCCESS = 0,
    AF_FAILURE = 1,
} afStatus;

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

void modifyBitsU16(uint16_t * regData, uint16_t data, uint8_t startBit, uint8_t numBits);
void modifyBitsU8(uint8_t * regData, uint8_t data, uint8_t startBit, uint8_t endBit);
uint8_t getBitsU8(uint8_t data, uint8_t startBit, uint8_t endBit);
uint8_t getBitsU8LE(uint8_t data, uint8_t startBit, uint8_t endBit);
uint16_t getBitsU16(uint16_t data, uint8_t startBit, uint8_t numBits);
uint32_t getRandomInt(uint32_t max);
uint32_t power(uint32_t num, uint8_t pow);
uint16_t isqrt(uint32_t x);
uint8_t log2RoundedDown(uint32_t value);
uint16_t arctan2(int16_t x, int16_t y);
uint32_t channelFromActiveChannel(uint32_t activeChannel);

int16_t fastSin16bit(uint16_t x);
int8_t fastSin8bit(uint8_t x);

typedef struct {
    uint8_t * data;
    uint16_t start;
    uint16_t end;
    uint16_t size;
    uint16_t maxUsage;
} sCircularBuffer;

// return of -1 means overflow
int8_t appendBuffer(sCircularBuffer * buffer, const uint8_t * bytes, uint16_t numBytes);
int8_t appendBufferWithTmpEnd(sCircularBuffer * buffer, uint16_t * tmpEnd, const uint8_t * bytes, uint16_t numBytes);
// return of -1 means underflow
int8_t popBuffer(sCircularBuffer * buffer, uint8_t * bytes, uint16_t numBytes);
uint16_t bufferLength(sCircularBuffer * buffer);
bool bufferHasSpace(sCircularBuffer * buffer, uint16_t numBytes);

unsigned incrAndWrap(unsigned val, unsigned inc, unsigned max);
int decrAndWrap(unsigned val, unsigned decr, unsigned max);

#define QUEUE_FULL(head, tail, size) ((head) == incrAndWrap(tail, 1, size))
#define QUEUE_EMPTY(head, tail, size) ((head) == (tail))
#define QUEUE_LENGTH(head, tail, size) (decrAndWrap(tail, head, size))
#define QUEUE_PEEK(entries, head) (&(entries)[(head)])

#define QUEUE_APPEND(entries, head, tail, size, newEntry) { \
    softAssert(!QUEUE_FULL(head, tail, size), "Trying to append to full queue"); \
    entries[tail] = newEntry; \
    tail = incrAndWrap(tail, 1, size); \
}

#define QUEUE_POP(head, tail, size) { \
    softAssert(!QUEUE_EMPTY(head, tail, size), "Trying to pop from empty queue"); \
    head = incrAndWrap(head, 1, size); \
}

#define BUFFER_FULL(buffer) (QUEUE_FULL((buffer).start, (buffer).end, (buffer).size))
#define BUFFER_EMPTY(buffer) (QUEUE_EMPTY((buffer).start, (buffer).end, (buffer).size))
#define BUFFER_LENGTH(buffer) (QUEUE_LENGTH((buffer).start, (buffer).end, (buffer).size))
#define BUFFER_APPEND(buffer, newEntry) (QUEUE_APPEND((buffer).entries, (buffer).start, (buffer).end, (buffer).size, newEntry))
#define BUFFER_PEEK(buffer) (QUEUE_PEEK((buffer).entries, (buffer).start))
#define BUFFER_POP(buffer) (QUEUE_POP((buffer).start, (buffer).end, (buffer).size))

/*
typedef struct {
	uint8_t psuedoStart;
	uint8_t start;
	uint8_t end;
	uint8_t size;
	uint8_t maxUsage;
	CanardCANFrame * entries;
} canBuffer;
#define BUFFER_PSUEDO_EMPTY(buffer) ((buffer).psuedoStart == (buffer).end)
#define BUFFER_PSUEDO_PEEK(buffer) (&((buffer).entries)[((buffer).psuedoStart)])
#define BUFFER_PSUEDO_POP(buffer) { 
    softAssert(((buffer).psuedoStart != (buffer).end), "Trying to pop from empty queue"); 
    (buffer).psuedoStart = incrAndWrap((buffer).psuedoStart, 1, (buffer).size); 
}
#define BUFFER_PSUEDO_RESET(buffer) {(buffer).psuedoStart = (buffer).start;}
*/
#ifdef F0
__attribute__((weak)) void setupCanHw();
#endif

#endif /* USEFUL_H */

