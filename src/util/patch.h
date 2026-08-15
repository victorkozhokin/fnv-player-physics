#pragma once

#include "game/types.h"

// Byte patching with verification.
//
// Every site this plugin touches is checked against the bytes the 1.4.0.525
// executable is known to contain. A mismatch means either the wrong game
// version or another plugin that got there first; in both cases writing anyway
// would corrupt unrelated code, so the patch is skipped and reported.

namespace patch {

// Number of patches that failed verification since load.
UInt32 FailureCount();

// Raw write, no verification. Used for the parts of a patch whose original
// bytes were already checked.
void Write(UInt32 address, const void *data, UInt32 size);

// Writes `patch` over `address` only if it currently holds `expected`.
// `name` appears in the log on mismatch. Returns false if it was skipped.
bool Verified(const char *name, UInt32 address,
              const void *expected, const void *data, UInt32 size);

template<UInt32 N, UInt32 M>
bool Verified(const char *name, UInt32 address,
              const char (&expected)[N], const char (&data)[M])
{
	static_assert(N == M, "patch and expected bytes must be the same length");
	return Verified(name, address, expected, data, N - 1);
}

// Redirects an existing `call rel32` at `address` to `hook`, returning the
// original target so the hook can chain to it. Returns null (and patches
// nothing) if `address` does not hold a call whose target is `expectedTarget`.
void *CallRel32(const char *name, UInt32 address, UInt32 expectedTarget, const void *hook);

// Overwrites a virtual table slot, returning the previous entry.
void *Vtable(UInt32 vtable, UInt32 index, const void *hook);

} // namespace patch
