#include "config.h"
#include "util/log.h"
#include "util/paths.h"
#include "util/win32.h"

namespace config {

Settings g_settings;

namespace {

constexpr auto kSection = "Physics";

// Minimal decimal parser -- enough for the "12.5" / "-0.75" forms an ini file
// holds, without dragging in strtod.
bool ParseFloat(const char *text, float *out)
{
	while (*text == ' ' || *text == '\t')
		text++;

	auto sign = 1.f;

	if (*text == '-' || *text == '+')
		sign = *text++ == '-' ? -1.f : 1.f;

	auto seenDigit = false;
	auto value = 0.f;

	for (; *text >= '0' && *text <= '9'; text++) {
		value = value * 10.f + float(*text - '0');
		seenDigit = true;
	}

	if (*text == '.') {
		text++;

		for (auto scale = .1f; *text >= '0' && *text <= '9'; text++, scale *= .1f) {
			value += float(*text - '0') * scale;
			seenDigit = true;
		}
	}

	if (!seenDigit)
		return false;

	*out = sign * value;
	return true;
}

float ReadFloat(const char *key, float fallback)
{
	char buffer[64];
	const auto length =
		GetPrivateProfileStringA(kSection, key, "", buffer, sizeof(buffer), paths::Ini());

	if (length == 0)
		return fallback;

	float value;

	if (!ParseFloat(buffer, &value)) {
		log::Print("  bad value for %s, using the default", key);
		return fallback;
	}

	return value;
}

bool ReadBool(const char *key, bool fallback)
{
	char buffer[16];
	const auto length =
		GetPrivateProfileStringA(kSection, key, "", buffer, sizeof(buffer), paths::Ini());

	if (length == 0)
		return fallback;

	return buffer[0] == '1' || buffer[0] == 't' || buffer[0] == 'T';
}

// The nine values each preset imposes. Anything not listed here -- slopes,
// interaction handling, the code patches -- always comes from the file.
struct PresetValues {
	const char *name;
	float friction, stopSpeed, acceleration, airAcceleration, airSpeed;
	float gravityMult, knockbackScale, knockbackMaxSpeed, landingPenalty;
};

// Indexed by Preset - 1; kPreset_Custom imposes nothing and has no entry.
constexpr PresetValues kPresets[] = {
	{"Responsive", 8.f, 11.0f, 10.f,  1.5f,  1.3f, 1.80f, 12.f, 1000.f, 160.f},
	{"Grounded",   7.f,  9.5f,  8.5f, 1.35f, 1.2f, 1.85f, 11.f,  950.f, 140.f},
	{"Heavy",      3.f,  5.0f,  4.f,  0.7f,  0.8f, 2.40f,  7.f,  750.f,  70.f},
};

static_assert(sizeof(kPresets) / sizeof(kPresets[0]) == kPreset_Count - 1);

void ApplyPreset(Settings &s)
{
	if (s.preset == kPreset_Custom || s.preset >= kPreset_Count) {
		log::Print("  preset: Custom (values come from this file)");
		return;
	}

	const auto &p = kPresets[s.preset - 1];

	s.friction                    = p.friction;
	s.stopSpeed                   = p.stopSpeed;
	s.acceleration                = p.acceleration;
	s.airAcceleration             = p.airAcceleration;
	s.airSpeed                    = p.airSpeed;
	s.gravityMult                 = p.gravityMult;
	s.knockbackScale              = p.knockbackScale;
	s.knockbackMaxSpeed           = p.knockbackMaxSpeed;
	s.landingPenaltyImpactSpeed50 = p.landingPenalty;

	log::Print("  preset: %s (overriding the nine values in the file)", p.name);
}

// Last-write time of the ini as of the last successful read, so the poll below
// can tell a real edit from a file that simply exists.
UInt32 g_stampLow  = 0;
UInt32 g_stampHigh = 0;

void RecordStamp()
{
	FileAttributeData info;

	if (GetFileAttributesExA(paths::Ini(), kGetFileExInfoStandard, &info) == 0)
		return;

	g_stampLow  = info.lastWriteLow;
	g_stampHigh = info.lastWriteHigh;
}

} // namespace

void Load()
{
	auto &s = g_settings;

	s.enabled                     = ReadBool ("bEnabled",                     s.enabled);
	s.friction                    = ReadFloat("fFriction",                    s.friction);
	s.acceleration                = ReadFloat("fAcceleration",                s.acceleration);
	s.airAcceleration             = ReadFloat("fAirAcceleration",             s.airAcceleration);
	s.stopSpeed                   = ReadFloat("fStopSpeed",                   s.stopSpeed);
	s.airSpeed                    = ReadFloat("fAirSpeed",                    s.airSpeed);
	s.gravityMult                 = ReadFloat("fGravityMult",                 s.gravityMult);
	s.knockbackScale              = ReadFloat("fKnockbackScale",              s.knockbackScale);
	s.landingPenaltyImpactSpeed50 = ReadFloat("fLandingPenaltyImpactSpeed50", s.landingPenaltyImpactSpeed50);
	s.knockbackMaxSpeed           = ReadFloat("fKnockbackMaxSpeed",           s.knockbackMaxSpeed);

	s.scaleFrictionBySlope = ReadBool("bScaleFrictionBySlope", s.scaleFrictionBySlope);
	s.clipVelocityToSlope  = ReadBool("bClipVelocityToSlope",  s.clipVelocityToSlope);

	s.vanillaKnockbackDuringInteraction =
		ReadBool("bVanillaKnockbackDuringInteraction", s.vanillaKnockbackDuringInteraction);
	s.yieldMovementDuringInteraction =
		ReadBool("bYieldMovementDuringInteraction", s.yieldMovementDuringInteraction);
	s.specialIdleFallback = ReadBool("bSpecialIdleFallback", s.specialIdleFallback);

	s.allowJumpWhileAiming    = ReadBool("bAllowJumpWhileAiming",    s.allowJumpWhileAiming);
	s.keepGroundZVelocity     = ReadBool("bKeepGroundZVelocity",     s.keepGroundZVelocity);
	s.bunnyhopGroundCollision = ReadBool("bBunnyhopGroundCollision", s.bunnyhopGroundCollision);

	if (const auto preset = ReadFloat("iPreset", float(s.preset));
	    preset >= 0.f && preset < float(kPreset_Count))
		s.preset = UInt32(preset);

	// After every read, so a preset wins over the nine values it covers while
	// everything it does not mention stays with whatever the file says.
	ApplyPreset(s);

	// Guard against values that would divide by zero or invert the physics.
	if (s.stopSpeed <= 0.f)
		s.stopSpeed = 1.f;

	RecordStamp();
}

bool ReloadIfChanged()
{
	FileAttributeData info;

	if (GetFileAttributesExA(paths::Ini(), kGetFileExInfoStandard, &info) == 0)
		return false;

	if (info.lastWriteLow == g_stampLow && info.lastWriteHigh == g_stampHigh)
		return false;

	Load();
	return true;
}

} // namespace config
