#ifndef jvm_h
#define jvm_h

/* jvm.h — Internal VM interfaces shared by jdo.c and jcode.c. */

#include "jass.h"
#include "jparser.h"

vmprogram_t VM_Compile(token_t const *token);

#endif /* jvm_h */