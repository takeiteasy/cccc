#pragma once
#define CHAIN_I 4
#if __has_include_next(<Chain/Chain.h>)
#define CHAIN_I_HAS_NEXT 1
#else
#define CHAIN_I_HAS_NEXT 0
#endif
#include_next <Chain/Chain.h>
