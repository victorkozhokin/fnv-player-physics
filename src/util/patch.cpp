#include "util/patch.h"
#include "util/log.h"
#include "util/win32.h"

namespace patch {
namespace {

UInt32 g_failures = 0;

bool Equal(const void *a, const void *b, UInt32 size)
{
	const auto *lhs = (const UInt8*)a;
	const auto *rhs = (const UInt8*)b;

	for (UInt32 i = 0; i < size; i++)
		if (lhs[i] != rhs[i])
			return false;

	return true;
}

void LogBytes(const char *label, const void *data, UInt32 size)
{
	for (UInt32 i = 0; i < size; i++)
		log::Print("    %s[%u] = %x", label, i, ((const UInt8*)data)[i]);
}

} // namespace

UInt32 FailureCount()
{
	return g_failures;
}

void Write(UInt32 address, const void *data, UInt32 size)
{
	DWORD oldProtect;

	if (!VirtualProtect((void*)address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		log::Print("  VirtualProtect failed at %x", address);
		g_failures++;
		return;
	}

	auto *target = (UInt8*)address;

	for (UInt32 i = 0; i < size; i++)
		target[i] = ((const UInt8*)data)[i];

	VirtualProtect((void*)address, size, oldProtect, &oldProtect);
}

bool Verified(const char *name, UInt32 address,
              const void *expected, const void *data, UInt32 size)
{
	// Another mod may have made the same edit already -- several tweak packs
	// change the same branches. The goal is met, so this is not a conflict.
	if (Equal((const void*)address, data, size)) {
		log::Print("  %s at %x is already applied", name, address);
		return true;
	}

	if (!Equal((const void*)address, expected, size)) {
		log::Print("  SKIPPED %s at %x: unexpected original bytes", name, address);
		LogBytes("found", (const void*)address, size);
		LogBytes("want ", expected, size);
		g_failures++;
		return false;
	}

	Write(address, data, size);
	return true;
}

void *CallRel32(const char *name, UInt32 address, UInt32 expectedTarget, const void *hook)
{
	const auto *site = (const UInt8*)address;

	if (site[0] != 0xE8) {
		log::Print("  SKIPPED %s at %x: not a call (opcode %x)", name, address, site[0]);
		g_failures++;
		return nullptr;
	}

	const auto rel = *(const SInt32*)(site + 1);
	const auto target = address + 5 + (UInt32)rel;

	// A target other than the stock one means another plugin detoured this
	// call first. Chain onto it rather than overwriting its work: our hook
	// takes the call and treats theirs as the original, so both stay live.
	if (target != expectedTarget)
		log::Print("  %s at %x: chaining onto %x, already hooked by another plugin",
		           name, address, target);

	const SInt32 newRel = (SInt32)((UInt32)hook - (address + 5));
	Write(address + 1, &newRel, sizeof(newRel));

	return (void*)target;
}

void *Vtable(UInt32 vtable, UInt32 index, const void *hook)
{
	auto **slot = (void**)vtable + index;
	const auto original = *slot;

	DWORD oldProtect;

	if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		log::Print("  VirtualProtect failed for vtable %x[%u]", vtable, index);
		g_failures++;
		return nullptr;
	}

	*slot = (void*)hook;
	VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);

	return original;
}

} // namespace patch
