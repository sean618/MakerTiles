/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.
 *
 * C++ port of myAssert.c. The assert field table is exposed over the field
 * protocol and therefore uses the fp:: types.
 */

#include <cstdint>
#include <cstring>

#include "project.h"
#include "myAssert.h"
#include "useful.h"
#include "FieldProtocol/fpCommon.hpp"

#define NUM_ASSERT_CHARS 127

static uint32_t assertCount = 0;
static char assertMsg[NUM_ASSERT_CHARS + 1] = {0};
static uint8_t assertPos = 0;

fp::FieldEntry assertFields[] = {
    {&assertCount, "assert_count",   1, fp::FieldDataType::Uint,      4,                fp::FieldFlags::Gettable, nullptr, nullptr, ""},
    {assertMsg,    "assert_message", 1, fp::FieldDataType::Utf8Char,  NUM_ASSERT_CHARS, fp::FieldFlags::Gettable, nullptr, nullptr, ""},
};
const fp::FieldTable assertFieldTable = {
    assertFields,
    sizeof(assertFields) / sizeof(fp::FieldEntry),
};

extern "C" void assertMessage(const char* msg, size_t msgLen) {
    if (assertPos < NUM_ASSERT_CHARS) {
        uint32_t numBytes = MIN(msgLen, (size_t)(NUM_ASSERT_CHARS - assertPos));
        std::memcpy(&assertMsg[assertPos], msg, numBytes);
        assertPos += numBytes;
    }
    assertCount++;
}
