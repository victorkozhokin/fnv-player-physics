#pragma once

#include "game/types.h"

// Settings read from Data\Config\PlayerMovement\PlayerPhysics.ini at load and
// re-read whenever the file changes -- see ReloadIfChanged below.
// The defaults reproduce the hardcoded values the plugin used to ship with.

namespace config {

// Built-in tunings for the nine values that give the movement its character.
// They live here rather than in files to be renamed so a menu can switch
// between them, and the change lands on the next reload of the ini.
enum Preset : UInt32 {
	kPreset_Custom = 0, // the file is in charge
	kPreset_Responsive,
	kPreset_Grounded,
	kPreset_Heavy,
	kPreset_Count
};

struct Settings {
	bool  enabled                    = true;

	// Which built-in tuning to impose over the nine values below.
	UInt32 preset                    = kPreset_Responsive;

	float friction                   = 5.f;
	float acceleration               = 6.f;
	float airAcceleration            = 1.f;
	float stopSpeed                  = 16.f;
	float airSpeed                   = 1.f;
	float gravityMult                = 2.f;
	float knockbackScale             = 10.f;
	float landingPenaltyImpactSpeed50 = 100.f;

	// Ceiling, in game units, on the speed a knockback may leave you with.
	// Applied to the result rather than to the impulse, so repeated blasts
	// cannot stack their way into orbit. Never slows you below the speed you
	// already had. 0 removes the limit.
	float knockbackMaxSpeed          = 900.f;


	// Interaction animation mods play a special idle and steer the player with
	// PushActorNoRagdoll every frame. The amplified one-shot knockback is
	// wrong for that, so let the engine handle knockback for the duration.
	bool  vanillaKnockbackDuringInteraction = true;

	// Hand movement over as well. The engine roots the player for these
	// animations and hook_CheckToRootCharacter suppresses that, so without
	// this the plugin keeps fighting the animation even with knockback given
	// back.
	bool  yieldMovementDuringInteraction = true;

	// Treat any special idle as an interaction when the script layer has not
	// said otherwise. Blunt -- it cannot tell which mod is playing what -- and
	// only there for installs without the bundled script.
	bool  specialIdleFallback         = true;

	// Scale ground friction and acceleration by the slope's normal, as the
	// original did. That weakens friction exactly where the ground is steepest,
	// which combined with the gravity multiplier makes the player drift down
	// slopes and struggle to climb.
	bool  scaleFrictionBySlope       = false;

	// Remove the velocity component heading into the ground plane while
	// supported. Vanilla MoveCharacter solves movement in a frame aligned to
	// the slope; this plugin replaces it and does not, so the component can
	// accumulate.
	bool  clipVelocityToSlope        = false;

	// Optional global code patches. These change behaviour for every
	// character, not just the player, so they can be turned off individually
	// if another mod conflicts.




	bool  allowJumpWhileAiming       = true;
	bool  keepGroundZVelocity        = true;
	bool  bunnyhopGroundCollision    = true;
};

extern Settings g_settings;

void Load();

// Re-reads the ini if it has been written since the last read, so changes made
// from the MCM menu -- or by hand, with the game running -- take effect without
// a restart. Cheap enough to call on a timer; it stats one file.
//
// Settings applied as code patches at load are deliberately not re-applied.
bool ReloadIfChanged();

} // namespace config
