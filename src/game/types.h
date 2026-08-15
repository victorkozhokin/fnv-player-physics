#pragma once

using UInt8  = unsigned char;
using UInt16 = unsigned short;
using UInt32 = unsigned int;
using SInt32 = int;
using uintptr_t_ = unsigned int;

static_assert(sizeof(void*) == 4, "this plugin is 32-bit only");

// Square root without pulling in the CRT.
inline float Sqrt(float x)
{
	float result;
	__asm {
		fld     x
		fsqrt
		fstp    result
	}
	return result;
}

// 2^x, used for the exponential landing penalty. Implemented on the x87 stack
// so the plugin stays free of a libm dependency.
inline float Exp2(float x)
{
	float result;
	__asm {
		fld     x
		fld     st(0)
		frndint             // int(x)
		fsub    st(1), st(0) // st1 = frac(x)
		fxch    st(1)
		f2xm1               // 2^frac(x) - 1
		fld1
		faddp   st(1), st(0) // 2^frac(x)
		fscale              // 2^frac(x) * 2^int(x)
		fstp    st(1)
		fstp    result
	}
	return result;
}

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }

struct NiVector3 {
	float x, y, z;

	constexpr NiVector3() : x(0), y(0), z(0) {}
	constexpr NiVector3(float x, float y, float z) : x(x), y(y), z(z) {}

	float LengthSqr() const { return x * x + y * y + z * z; }
	float Length() const { return Sqrt(LengthSqr()); }

	float Dot(const NiVector3 &o) const { return x * o.x + y * o.y + z * o.z; }

	NiVector3 Cross(const NiVector3 &o) const
	{
		return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
	}

	// Returns false (leaving the vector untouched) for a degenerate input
	// instead of producing NaNs, which would propagate into the player's
	// velocity and wedge the character controller.
	bool Normalize()
	{
		const auto lengthSqr = LengthSqr();

		if (lengthSqr < 1e-12f)
			return false;

		const auto scale = 1.f / Sqrt(lengthSqr);
		x *= scale;
		y *= scale;
		z *= scale;
		return true;
	}

	NiVector3 operator+(const NiVector3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
	NiVector3 operator-(const NiVector3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
	NiVector3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

// Havok's hkVector4: four floats on a 16 byte boundary. The game reads and
// writes these with movaps, so the alignment is part of the ABI.
struct alignas(16) AlignedVector4 {
	float x, y, z, w;

	constexpr AlignedVector4() : x(0), y(0), z(0), w(0) {}
	constexpr AlignedVector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
	constexpr explicit AlignedVector4(const NiVector3 &v) : x(v.x), y(v.y), z(v.z), w(0) {}

	NiVector3 XYZ() const { return {x, y, z}; }

	AlignedVector4 operator+(const AlignedVector4 &o) const
	{
		return {x + o.x, y + o.y, z + o.z, w + o.w};
	}

	AlignedVector4 operator-(const AlignedVector4 &o) const
	{
		return {x - o.x, y - o.y, z - o.z, w - o.w};
	}

	AlignedVector4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }

	AlignedVector4 &operator+=(const AlignedVector4 &o) { return *this = *this + o; }
	AlignedVector4 &operator-=(const AlignedVector4 &o) { return *this = *this - o; }
	AlignedVector4 &operator*=(float s) { return *this = *this * s; }

	bool IsFinite() const
	{
		// NaN and infinity both have an all-ones exponent field.
		const auto bad = [](float f) {
			const auto bits = __builtin_bit_cast(UInt32, f);
			return (bits & 0x7F800000u) == 0x7F800000u;
		};
		return !bad(x) && !bad(y) && !bad(z);
	}
};

static_assert(sizeof(AlignedVector4) == 0x10);
static_assert(alignof(AlignedVector4) == 0x10);

// Function pointer to void* without tripping -Wmicrosoft-cast. Both are 4 bytes
// on this target, which is all the patching code needs.
template<typename T>
inline void *AsCodePtr(T function)
{
	static_assert(sizeof(T) == sizeof(void*));
	return __builtin_bit_cast(void*, function);
}

// Calling-convention helpers for game functions at fixed addresses.
template<typename T = void, typename ...Args>
inline T CdeclCall(UInt32 address, Args ...args)
{
	return ((T(__cdecl*)(Args...))address)(args...);
}

template<typename T = void, typename ...Args>
inline T ThisCall(UInt32 address, void *self, Args ...args)
{
	return ((T(__thiscall*)(void*, Args...))address)(self, args...);
}
