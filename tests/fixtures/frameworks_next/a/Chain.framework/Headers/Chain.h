#pragma once
#define CHAIN_A 1
#if __has_include_next(<Chain/Chain.h>)
#define CHAIN_A_HAS_NEXT 1
#else
#define CHAIN_A_HAS_NEXT 0
#endif
#include_next <Chain/Chain.h>
