#ifndef ASSERT_H
#define ASSERT_H

#include "project.h"

#ifndef STRINGIFY
#define STRINGIFY(x) #x
#endif
#ifndef TOSTRING
#define TOSTRING(x) STRINGIFY(x)
#endif

#define softAssert(_pred_, _msg_) myAssert(_pred_, _msg_)
#define myAssert(_pred_, _msg_)       myAssertLL(_pred_, __FILE__ ": " TOSTRING(__LINE__) " - " _msg_)
#define myAssertLL(_pred_, _msg_) if ((_pred_) == false) assertMessage( _msg_ , sizeof(_msg_))

#ifdef __cplusplus
extern "C" {
#endif

// Records an assertion failure (appends the message, bumps the counter).
// C-callable so the C HAL/USB layers can use the myAssert macro too.
void assertMessage(const char * msg, size_t msgLen);

#ifdef __cplusplus
}  // extern "C"

// The assert fields are exposed over the field protocol, so the table uses the
// C++ fp:: types. Only visible to C++ translation units (e.g. board.cpp).
#include "FieldProtocol/fpCommon.hpp"
extern const fp::FieldTable assertFieldTable;
#endif

#endif
