// Freestanding runtime support.
//
// The plugin links without a C runtime so it can be cross-compiled without a
// Visual Studio installation. These are the only symbols the compiler may still
// emit references to.

#include "game/types.h"

// Nothing in this file has a caller inside it, so link-time optimisation would
// happily drop the lot.
#define KEEP __attribute__((used))

extern "C" {

// MSVC-compatible object files must define this when floating point is used.
KEEP int _fltused = 0;

KEEP void *memcpy(void *dst, const void *src, unsigned int size)
{
	auto *out = (UInt8*)dst;
	const auto *in = (const UInt8*)src;

	for (unsigned int i = 0; i < size; i++)
		out[i] = in[i];

	return dst;
}

KEEP void *memset(void *dst, int value, unsigned int size)
{
	auto *out = (UInt8*)dst;

	for (unsigned int i = 0; i < size; i++)
		out[i] = (UInt8)value;

	return dst;
}

KEEP int __stdcall DllMain(void *instance, unsigned int reason, void *reserved)
{
	return 1;
}

} // extern "C"
