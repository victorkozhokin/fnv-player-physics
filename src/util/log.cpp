#include "util/log.h"
#include "util/paths.h"
#include "util/win32.h"

namespace log {
namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;

struct Buffer {
	char  data[512];
	UInt32 length = 0;

	void Push(char c)
	{
		if (length < sizeof(data) - 1)
			data[length++] = c;
	}

	void Push(const char *text)
	{
		if (text == nullptr)
			text = "(null)";

		while (*text != '\0')
			Push(*text++);
	}

	void PushUInt(UInt32 value, UInt32 base)
	{
		char digits[32];
		UInt32 count = 0;

		do {
			const auto digit = value % base;
			digits[count++] = digit < 10 ? char('0' + digit) : char('a' + digit - 10);
			value /= base;
		} while (value != 0);

		while (count-- > 0)
			Push(digits[count]);
	}
};

} // namespace

void Open()
{
	if (g_file != INVALID_HANDLE_VALUE)
		return;

	g_file = CreateFileA(paths::Log(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
	                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Close()
{
	if (g_file == INVALID_HANDLE_VALUE)
		return;

	CloseHandle(g_file);
	g_file = INVALID_HANDLE_VALUE;
}

void Print(const char *format, ...)
{
	Buffer buffer;

	__builtin_va_list args;
	__builtin_va_start(args, format);

	for (const char *p = format; *p != '\0'; p++) {
		if (*p != '%') {
			buffer.Push(*p);
			continue;
		}

		switch (*++p) {
		case 's': buffer.Push(__builtin_va_arg(args, const char*));   break;
		case 'u': buffer.PushUInt(__builtin_va_arg(args, UInt32), 10); break;
		case 'x': buffer.PushUInt(__builtin_va_arg(args, UInt32), 16); break;
		case '%': buffer.Push('%');                                    break;
		case '\0': p--;                                                break;
		default:  buffer.Push('%'); buffer.Push(*p);                   break;
		}
	}

	__builtin_va_end(args);

	buffer.Push('\r');
	buffer.Push('\n');
	buffer.data[buffer.length] = '\0';

	OutputDebugStringA(buffer.data);

	if (g_file != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteFile(g_file, buffer.data, buffer.length, &written, nullptr);
	}
}

} // namespace log
