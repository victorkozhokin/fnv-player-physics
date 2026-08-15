#pragma once

// Tiny logger writing to Data\NVSE\Plugins\PlayerPhysics.log.
//
// Supports %s (const char*), %u (UInt32), %x (UInt32, lowercase hex) and %%.
// Deliberately no float support -- nothing worth logging here is a float, and
// a float formatter would be the only reason to link a CRT.

namespace log {

void Open();
void Close();
void Print(const char *format, ...);

} // namespace log
