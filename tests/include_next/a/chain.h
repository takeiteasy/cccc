#pragma once
#include <other.h>
#include_next <chain.h>
#if __has_include_next(<chain.h>)
#define CHAIN_A_HAS_NEXT 1
#else
#define CHAIN_A_HAS_NEXT 0
#endif
#define CHAIN_A 1
