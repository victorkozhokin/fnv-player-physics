#include "config.h"
#include "game/game.h"
#include "nvse_api.h"
#include "util/log.h"
#include "util/paths.h"
#include "util/patch.h"
#include "util/win32.h"

using namespace hkpCharacterState;
using namespace ActorMover;

// Game units per Havok unit.
constexpr auto kHavokUnitScale = 1.f / 6.9991255f;

enum ControlState {
	kControlState_Held    = 0,
	kControlState_Pressed = 1
};

//----------------------------------------------------------------------------
// Plugin state
//----------------------------------------------------------------------------

namespace {

struct PlayerState {
	AlignedVector4 airVelocity;

	// Frame counter, bumped once per character state update of the player.
	//
	// A landing is spotted by the in-air state and consumed by the on-ground
	// state on the following frame, so the flag is given a one frame window.
	// Without that bound a landing followed by anything other than an
	// on-ground update (a ladder, furniture, a load) left the flag stuck and
	// disabled ground friction indefinitely.
	UInt32 tick       = 0;
	UInt32 landedTick = 0;
	bool   landed     = false;

	bool   usedJumpInput  = true;




	// Vanilla gravity multiplier, captured before the plugin first overrides
	// it so it can be put back when the physics are inactive.
	float  vanillaGravityMult = 1.f;
	bool   gravityCaptured    = false;

	void Reset()
	{
		airVelocity   = AlignedVector4();
		landed        = false;
		usedJumpInput  = true;

	}

	bool JustLanded() const { return landed && tick - landedTick <= 1; }
};

constinit PlayerState g_player;

// Original targets of every detour, filled in during installation.
// Frames until the next look at the ini's timestamp.
constinit UInt32 g_reloadCountdown = 0;

// Set by the script layer for as long as an interaction animation is steering
// the player. See the Script interface section for why this is not inferred.
// How long the player has been asking to move and not getting anywhere. Lives
// here rather than in a plugin of its own because this is the only code that
// sees both halves of the question: the speed the engine asked for, and the
// distance actually covered. A separate plugin cannot even chain onto this --
// once the physics take over, the original MoveCharacter is never called.
constinit float g_blockedSeconds = 0.f;
constinit NiVector3 g_lastPos;
constinit bool  g_haveLastPos = false;
constinit UInt32 g_lastBlockedTenths = 0;

// Off the ground. Reported for the same reason as the blocked time: the mantling
// script has to know, and the character controller's own state is the only
// honest answer -- an actor can be a foot in the air with every animation still
// claiming otherwise. Note that "blocked" is meaningless up here, because
// nothing stops a jump against a wall, which is why this is a separate question
// rather than a longer wait on the old one.
constinit bool  g_inAir = false;

// The same two numbers the blocked timer is built from, as a ratio, and the time
// spent off the ground.
//
// The timer answers "has the player been stuck for a while", which needs history
// and therefore cannot answer on the first frame. The ratio answers "is the
// player getting anywhere right now", which needs none and answers immediately.
// A climb wants the second question -- the wait in front of the first was never
// wanted, it was the cost of asking it.
constinit float g_speedRatio  = 1.f;
constinit float g_airSeconds  = 0.f;

constinit bool  g_scriptInteraction = false;
constinit bool  g_scriptLayerSeen   = false;
constinit float g_interactionSeconds = 0.f;

// An interaction that never reports finishing would disable the physics for the
// rest of the session, so the flag lapses on its own well after any real one.
constexpr auto kInteractionWatchdogSeconds = 30.f;

constinit void *g_origMoveCharacter        = nullptr;
constinit void *g_origCheckJumpButton      = nullptr;
constinit void *g_origJumpingUpdate        = nullptr;
constinit void *g_origOnGroundUpdate       = nullptr;
constinit void *g_origInAirUpdate          = nullptr;
constinit void *g_origUpdateCharacterState = nullptr;
constinit void *g_origGetFallDistance      = nullptr;
constinit void *g_origUpdateThrowback      = nullptr;

} // namespace

//----------------------------------------------------------------------------
// Predicates
//----------------------------------------------------------------------------

static bool IsPlayerController(const bhkCharacterController *charCtrl)
{
	if (charCtrl == nullptr)
		return false;

	const auto *player = PlayerCharacter::GetSingleton();

	if (player == nullptr)
		return false;

	return charCtrl == player->GetCharacterController();
}

// Whether the custom movement code should drive the player this frame.
//
// Everything below is a case where the engine, a script or another mod owns
// the player's motion. Taking over in those states is what used to produce
// crashes and stuck poses with animation mods such as B42 Interact.
static bool IsSpecialIdlePlaying();
static bool IsInteractionActive();

extern "C" bool __cdecl ShouldUsePhysics(const bhkCharacterController *charCtrl)
{
	if (!config::g_settings.enabled)
		return false;

	const auto *player = PlayerCharacter::GetSingleton();

	if (player == nullptr || charCtrl == nullptr)
		return false;

	if (charCtrl != player->GetCharacterController())
		return false;

	if (VATSCameraData::Get()->mode != 0)
		return false;

	// Dead, unconscious or restrained.
	if (player->lifeState != 0)
		return false;

	// Sitting, sleeping or otherwise in furniture.
	if (player->sitSleepState != 0)
		return false;

	// Water is handled by IsSwimming and the gravity hook rather than here. See
	// the note there for why the whole plugin no longer stands down for it.

	// Movement controls taken away by a script -- scripted sequences and
	// animation mods do this while they play an idle on the player.
	if ((player->pcControlFlags & PlayerCharacter::kControlFlag_Movement) != 0)
		return false;

	// May be null while the player has no 3D loaded. The original code
	// dereferenced this unconditionally.
	const auto *animData = player->GetAnimData();

	if (animData == nullptr)
		return false;

	if (IsMovementOverrideSequence(animData->animGroupIDs[AnimData::kSequence_Weapon]))
		return false;

	// An interaction animation is running. The engine wants to root the player
	// in place for it, and hook_CheckToRootCharacter suppresses exactly that,
	// so staying in control here means fighting the animation for the player.
	if (config::g_settings.yieldMovementDuringInteraction && IsInteractionActive())
		return false;

	return true;
}

// In water deep enough that the player is swimming rather than walking.
//
// Two signals, because they answer at different moments. The character
// controller enters its swimming state only once the water is deep enough to
// swim in; the move flag is set by the game's own movement code and catches
// wading out of the shallows a little before that. Answering early is free
// here -- all it costs is vanilla gravity while still knee-deep.
static bool IsSwimming(const bhkCharacterController *charCtrl)
{
	if (charCtrl != nullptr && charCtrl->hkState == kState_Swimming)
		return true;

	const auto *player = PlayerCharacter::GetSingleton();

	if (player == nullptr)
		return false;

	const auto *mover = player->actorMover;

	return mover != nullptr && (mover->pcMovementFlags & kMoveFlag_Swimming) != 0;
}

static bool HasSpecialIdle(const AnimData *animData)
{
	return animData != nullptr &&
	       animData->animGroupIDs[AnimData::kSequence_SpecialIdle] != AnimData::kAnimGroup_None;
}

// Whether an interaction animation is steering the player right now.
//
// The script layer is asked first, because it knows: it is told by the
// interaction mod itself, at the moment the mod starts pulling the player and
// again when it stops. Nothing here has to be inferred.
//
// The special idle test below is the fallback for a game without that script,
// and it is blunt on purpose -- it cannot tell "this mod is dragging the
// player" from "some mod is playing an animation", so it stands the physics
// down for every special idle in the load order.
static bool IsInteractionActive()
{
	// Once the script layer has announced itself it is the only answer. Falling
	// back to the special idle test while it is present would put the old
	// behaviour straight back: the physics would still stand down for every
	// animation in the load order, and the script would merely be adding
	// precision on top of a check that was never precise.
	if (g_scriptLayerSeen)
		return g_scriptInteraction;

	return config::g_settings.specialIdleFallback && IsSpecialIdlePlaying();
}

// Note this is *any* special idle, not just a movement-override one: the anim
// groups those mods play are not movement overrides, so the check used for the
// weapon sequence lets them straight through.
//
// All three animation sets are inspected. Interaction mods ship first person
// and third person variants, and GetAnimData only reports whichever set the
// current camera makes active, so testing that one alone would miss the
// animation depending on the player's point of view.
static bool IsSpecialIdlePlaying()
{
	const auto *player = PlayerCharacter::GetSingleton();

	if (player == nullptr)
		return false;

	return HasSpecialIdle(player->GetAnimData()) ||
	       HasSpecialIdle(player->firstPersonAnim) ||
	       HasSpecialIdle(player->GetThirdPersonAnimData());
}

// The amplified one-shot knockback collapses what the engine spreads over
// several frames into a single impulse, which only holds for a one-off shove.
// A script pushing the player every frame -- interaction mods steering him
// towards an object -- needs the engine's own handling instead.
static bool UseCustomKnockback(const bhkCharacterController *charCtrl)
{
	if (!ShouldUsePhysics(charCtrl))
		return false;

	return !(config::g_settings.vanillaKnockbackDuringInteraction && IsInteractionActive());
}

//----------------------------------------------------------------------------
// Movement
//----------------------------------------------------------------------------

// The original scaled both friction and acceleration by the ground normal's Z,
// which is 1 on the flat and shrinks as the ground steepens -- so grip was
// weakest exactly where the slope pulls hardest. With the doubled gravity that
// reads as drifting down hills and struggling to climb them.
static float SlopeScale(const CharacterMoveParams &move)
{
	if (!config::g_settings.scaleFrictionBySlope)
		return 1.f;

	// A negative normal would turn friction into acceleration.
	return Max(move.groundNormal.z, 0.f);
}

// Removes the part of the velocity heading into the ground, leaving the rest
// running along it. Vanilla MoveCharacter solves movement in a frame built from
// the slope; this plugin replaces that wholesale, so without this step the
// into-surface component has nothing taking it back out.
static void ClipToSlope(const CharacterMoveParams &move, AlignedVector4 *velocity)
{
	const auto normal = move.groundNormal.XYZ();

	if (normal.LengthSqr() < 1e-6f)
		return;

	const auto into = velocity->XYZ().Dot(normal);

	if (into >= 0.f)
		return;

	*velocity -= AlignedVector4(normal * into);
}

// `baseSpeed` is the top speed the player can currently reach, which crouching
// and walking cut well below the running speed the settings were tuned around.
static void ApplyFriction(const CharacterMoveParams &move, AlignedVector4 *velocity,
                          float baseSpeed, float deltaTime)
{
	const auto &settings = config::g_settings;
	const auto speed = velocity->XYZ().Length();

	if (speed <= 0.f)
		return;

	// fStopSpeed is a floor stopping friction from tailing off asymptotically,
	// but it is one absolute figure tuned against running speed. A crouching
	// player tops out at roughly two thirds of that, so the same number is a
	// far larger share of the speed they can actually reach and crouching
	// drags -- by an amount that depends on how high the preset set it.
	//
	// Clamping it to the current top speed is not enough: at 22 against a
	// crouch ceiling of ~37 no clamping happens at all, and the drag is back.
	// Hold it at the same *proportion* of whatever top speed is in force
	// instead, so crouching, walking and running all decelerate alike.
	// fStopSpeed is the speed below which friction stops easing off, so that a
	// slow crawl still comes to a stop instead of approaching it forever.
	//
	// Left unbounded it can stop movement outright. Below it, friction is a
	// constant fFriction * fStopSpeed while acceleration is only
	// fAcceleration * topSpeed, so anything slow enough loses the race and
	// decelerates to a standstill with the key still held. Crouching tops out
	// around 6.9 here, and Responsive's 11 was well past the point where that
	// happens -- switch to walking, drift down, and the character parks.
	//
	// Bounding it by the top speed in force makes the outcome depend only on
	// fAcceleration against fFriction, which is a ratio the presets already
	// keep above one, rather than on an absolute figure that happens to suit
	// running.
	// Only while there is input to lose the race to. With the keys released
	// there is no acceleration to outrun, and the floor should apply in full so
	// the character stops crisply instead of coasting to a halt for ever.
	const auto stopSpeed  = baseSpeed > 0.f
		? Min(settings.stopSpeed, baseSpeed)
		: settings.stopSpeed;
	const auto scaleSpeed = Max(speed, stopSpeed);

	const auto friction   = settings.friction * scaleSpeed *
	                        SlopeScale(move) * deltaTime;

	if (friction >= speed)
		*velocity = AlignedVector4();
	else
		*velocity *= 1.f - friction / speed;
}

static void ApplyAcceleration(const CharacterMoveParams &move, AlignedVector4 *velocity,
                              const NiVector3 &moveVector, bool inAir, float baseSpeed,
                              float deltaTime)
{
	const auto &settings = config::g_settings;

	const auto speed    = velocity->XYZ().Dot(moveVector);
	const auto maxSpeed = inAir ? baseSpeed * settings.airSpeed : baseSpeed;
	const auto speedCap = Max(baseSpeed, velocity->XYZ().Length());

	if (speed >= maxSpeed)
		return;

	const auto accelMultiplier = inAir ? settings.airAcceleration : settings.acceleration;

	const auto accel = Max(accelMultiplier * baseSpeed * SlopeScale(move) * deltaTime, 0.f);

	*velocity += AlignedVector4(moveVector * Min(accel, maxSpeed - speed));

	if (const auto newLength = velocity->XYZ().Length(); newLength > speedCap)
		*velocity *= speedCap / newLength;
}

static NiVector3 GetInputVector(UInt32 moveFlags)
{
	auto result = NiVector3();

	if (moveFlags & kMoveFlag_Forward)
		result.x = 1.f;
	else if (moveFlags & kMoveFlag_Backward)
		result.x = -1.f;

	if (moveFlags & kMoveFlag_Left)
		result.y = -1.f;
	else if (moveFlags & kMoveFlag_Right)
		result.y = 1.f;

	return result;
}

// Returns false when no usable direction could be built, rather than handing
// back a NaN vector that would poison the player's velocity.
static bool GetMoveVector(const CharacterMoveParams &move, const NiVector3 &input,
                          NiVector3 *out)
{
	const auto forward = move.forward.XYZ();
	const auto up      = move.up.XYZ();

	// JIP's NiVector3::CrossProduct(b) evaluates b x this, not this x b, and
	// normalizes the result. Writing the cross product the usual way round
	// mirrored the strafe axis: forward-left walked forward-right.
	auto right = up.Cross(forward);
	right.Normalize();

	auto moveVector = forward * -input.x + right * input.y + up * input.z;

	if (!moveVector.Normalize())
		return false;

	const auto &normal = move.groundNormal;

	if (normal.z <= 1e-4f || normal.z >= 1.f - 1e-4f) {
		*out = moveVector;
		return true;
	}

	// Project onto the slope.
	const auto dot = moveVector.Dot(normal.XYZ());
	auto projected = NiVector3(moveVector.x, moveVector.y, -dot / normal.z);

	if (!projected.Normalize())
		return false;

	*out = projected;
	return true;
}

// The base ground speed in Havok units.
//
// The original source read a PlayerMover::moveSpeed field that does not exist
// in any public header for this game version -- PlayerMover holds an analog
// move *direction* at that offset, not a speed. The engine's cached walk and
// run speeds are used instead, falling back to the configured value while the
// cache is cold.
// The player's running speed, which is what fStopSpeed was tuned against and so
// the reference the friction floor is held in proportion to.
static void UpdateVelocity(const CharacterMoveParams &move, AlignedVector4 *velocity,
                           UInt32 state, float deltaTime)
{
	const auto inAir = state == kState_InAir || g_player.JustLanded();

	constexpr auto kMoveMask =
		kMoveFlag_Forward | kMoveFlag_Backward | kMoveFlag_Left | kMoveFlag_Right;

	const auto *player = PlayerCharacter::GetSingleton();
	const auto *mover  = player != nullptr ? player->actorMover : nullptr;
	const auto moveFlags = mover != nullptr ? mover->pcMovementFlags : 0u;

	// The engine's own wanted-movement vector. Its length is the speed the
	// player should be moving at, with stance, encumbrance, crippled legs and
	// whatever any speed mod has done already in it -- which is why upstream
	// never needed a speed of its own, and why every attempt here to rebuild
	// one from cached fields and multipliers broke crouching.
	const auto baseSpeed = move.input.XYZ().Length();


	if (!inAir) {
		if (config::g_settings.clipVelocityToSlope)
			ClipToSlope(move, velocity);

		ApplyFriction(move, velocity, baseSpeed, deltaTime);
	}

	if (mover == nullptr || (moveFlags & kMoveMask) == 0)
		return;

	const auto inputVector = GetInputVector(moveFlags);

	if (NiVector3 moveVector; GetMoveVector(move, inputVector, &moveVector))
		ApplyAcceleration(move, velocity, moveVector, inAir, baseSpeed, deltaTime);
}

static void ApplyThrowback(bhkCharacterController *charCtrl)
{
	if (charCtrl->throwbackTimer <= 0.f || !charCtrl->ReceivesThrowback())
		return;

	// Scale based on total distance moved in vanilla. This converts what the
	// engine spreads over many frames into one impulse, which is only
	// equivalent for a one-off shove.
	const auto scale = charCtrl->throwbackTimer * charCtrl->throwbackTimer * .5f;
	const auto impulse =
		charCtrl->throwbackVelocity * (scale * config::g_settings.knockbackScale);

	const auto before = charCtrl->velocity.XYZ().Length();
	charCtrl->velocity += impulse;

	// Cap the result rather than the impulse. Two grenades landing together, or
	// a script shoving the player every frame, each get their own impulse and
	// used to add up without limit; a ceiling on the outcome lets the first
	// blast throw you properly and stops the rest compounding. Whatever speed
	// you already carried is never taken away.
	const auto ceiling = config::g_settings.knockbackMaxSpeed * kHavokUnitScale;

	if (ceiling > 0.f) {
		const auto limit = Max(before, ceiling);

		if (const auto after = charCtrl->velocity.XYZ().Length(); after > limit)
			charCtrl->velocity *= limit / after;
	}

	charCtrl->throwbackTimer = 0.f;
	charCtrl->throwbackVelocity = AlignedVector4();
}

//----------------------------------------------------------------------------
// Script interface
//----------------------------------------------------------------------------
//
// Two commands, so an interaction mod can say when it is steering the player
// instead of leaving the plugin to guess from the animation system.
//
// Guessing was the old behaviour and it was always too broad. The engine can be
// asked whether *a* special idle is playing, but not whose, nor whether that
// mod is moving the player or merely animating him -- so standing the physics
// down meant standing them down for every animation mod in the load order.
//
// B42 Interact dispatches an event at the exact moment it starts pulling the
// player towards an object, with the duration in hand. A dozen lines of script
// turn that into these two calls, and the guessing goes away.

// Called once as the script loads, not when an interaction starts. That is the
// whole point: the plugin has to know the layer is there *before* the first
// interaction, or it spends until then guessing from special idles.
extern "C" bool __cdecl Cmd_PPScriptLayerReady_Execute(void*, void*, void*, void*,
                                                       void*, void*, double *result,
                                                       UInt32*)
{
	*result = 1.0;

	if (!g_scriptLayerSeen) {
		g_scriptLayerSeen = true;
		log::Print("Script layer connected; the special idle fallback is now unused.");
	}

	return true;
}

extern "C" bool __cdecl Cmd_PPBeginInteraction_Execute(void*, void*, void*, void*,
                                                       void*, void*, double *result,
                                                       UInt32*)
{
	*result = 1.0;
	g_scriptInteraction  = true;
	g_interactionSeconds = 0.f;
	return true;
}

// Seconds the player has been walking into something without getting anywhere.
// Zero means they are moving. This is what the mantling script waits on.
extern "C" bool __cdecl Cmd_PPBlockedTime_Execute(void*, void*, void*, void*,
                                                  void*, void*, double *result,
                                                  UInt32*)
{
	*result = g_blockedSeconds;
	return true;
}

// Whether the character controller has left the ground. The mantling script
// asks because being stopped by something is not a signal it can get while
// jumping -- a jump into a wall is not stopped, it simply arrives.
extern "C" bool __cdecl Cmd_PPInAir_Execute(void*, void*, void*, void*,
                                            void*, void*, double *result,
                                            UInt32*)
{
	*result = g_inAir ? 1.0 : 0.0;
	return true;
}

// How much of the movement the engine asked for the player is actually getting,
// 0 to 1. Low means walking into something. Unlike the blocked timer this is
// true on the frame it happens, which is the whole reason it exists.
extern "C" bool __cdecl Cmd_PPSpeedRatio_Execute(void*, void*, void*, void*,
                                                 void*, void*, double *result,
                                                 UInt32*)
{
	*result = g_speedRatio;
	return true;
}

// Seconds since the character controller left the ground. Zero on the ground.
// A climb wants this rather than a plain airborne flag: the first moments of a
// jump are spent at ankle height, where nothing is within reach yet.
extern "C" bool __cdecl Cmd_PPAirTime_Execute(void*, void*, void*, void*,
                                              void*, void*, double *result,
                                              UInt32*)
{
	*result = g_airSeconds;
	return true;
}

extern "C" bool __cdecl Cmd_PPEndInteraction_Execute(void*, void*, void*, void*,
                                                     void*, void*, double *result,
                                                     UInt32*)
{
	*result = 1.0;
	g_scriptInteraction = false;
	return true;
}

// Opcode space for plugins runs from 0x2000 to 0x8000, and 0x2000 itself is the
// "unassigned" base NVSE complains about. Ranges are handed out by the NVSE
// team to keep plugins apart; this one is not from them, it is a sparse corner
// picked to make a collision unlikely. Seven slots are claimed.
//
// A collision here fails loudly rather than quietly: the script is compiled at
// runtime by name, so a displaced command shows up as a compile error in the
// log instead of as the wrong function being called.
inline constexpr UInt32 kOpcodeBase = 0x6A00;

// Not const: NVSE writes the assigned opcode into these during registration.
CommandInfo g_cmdBeginInteraction = {
	"PPBeginInteraction", "", 0,
	"tells Player Physics an interaction animation is steering the player",
	0, 0, nullptr, AsCodePtr(Cmd_PPBeginInteraction_Execute), nullptr, nullptr, 0
};

CommandInfo g_cmdScriptLayerReady = {
	"PPScriptLayerReady", "", 0,
	"tells Player Physics the script layer is installed",
	0, 0, nullptr, AsCodePtr(Cmd_PPScriptLayerReady_Execute), nullptr, nullptr, 0
};

CommandInfo g_cmdBlockedTime = {
	"PPBlockedTime", "", 0,
	"seconds the player has been walking into something without moving",
	0, 0, nullptr, AsCodePtr(Cmd_PPBlockedTime_Execute), nullptr, nullptr, 0
};

CommandInfo g_cmdInAir = {
	"PPInAir", "", 0,
	"whether the player's character controller has left the ground",
	0, 0, nullptr, AsCodePtr(Cmd_PPInAir_Execute), nullptr, nullptr, 0
};

CommandInfo g_cmdSpeedRatio = {
	"PPSpeedRatio", "", 0,
	"how much of the movement the engine asked for the player is getting, 0 to 1",
	0, 0, nullptr, AsCodePtr(Cmd_PPSpeedRatio_Execute), nullptr, nullptr, 0
};

CommandInfo g_cmdAirTime = {
	"PPAirTime", "", 0,
	"seconds since the player's character controller left the ground",
	0, 0, nullptr, AsCodePtr(Cmd_PPAirTime_Execute), nullptr, nullptr, 0
};

CommandInfo g_cmdEndInteraction = {
	"PPEndInteraction", "", 0,
	"tells Player Physics the interaction animation has finished",
	0, 0, nullptr, AsCodePtr(Cmd_PPEndInteraction_Execute), nullptr, nullptr, 0
};

//----------------------------------------------------------------------------
// Hooks
//----------------------------------------------------------------------------

// extern "C" and externally visible so the naked wrapper below keeps a
// reference the optimiser cannot strip.
extern "C" void __cdecl hook_MoveCharacter(bhkCharacterController *charCtrl,
                                           CharacterMoveParams *move, AlignedVector4 *velocity)
{
	const auto callOriginal = [&] {
		((void(__cdecl*)(CharacterMoveParams*, AlignedVector4*))g_origMoveCharacter)(
			move, velocity);
	};

	// Everything from here to the ownership test has to run whether or not this
	// plugin is driving the player, and all of it used to sit below the test.
	//
	// That one misplacement was three bugs. The ini reload could never undo
	// itself: bEnabled=0 makes ShouldUsePhysics false, the function returned
	// before reaching the reload, and nothing was left running that could ever
	// read bEnabled=1 again -- so the MCM switch worked once, in one direction,
	// until the game was restarted. The interaction watchdog had the same shape
	// and was worse: a raised interaction flag is *itself* what makes
	// ShouldUsePhysics false, so the timeout meant to release a flag nobody
	// lowered could not run for exactly as long as it was needed. And the
	// blocked, airborne and speed-ratio figures, which are what Mantle reads,
	// froze at whatever they last were while the physics were stood down.
	//
	// Observing and acting are separate jobs. This half observes.
	const auto state     = charCtrl->hkState;
	const auto deltaTime = charCtrl->deltaTime;

	if (!IsPlayerController(charCtrl)) {
		callOriginal();
		return;
	}

	// The interaction flag lapses if whoever set it never comes back.
	if (g_scriptInteraction) {
		g_interactionSeconds += deltaTime;

		if (g_interactionSeconds > kInteractionWatchdogSeconds) {
			g_scriptInteraction = false;
			log::Print("Interaction flag timed out; releasing movement.");
		}
	}

	// Pick up MCM edits, and hand edits made with the game running, without a
	// restart. About once a second, and it does no more than stat a file until
	// an edit actually lands.
	//
	// The three code patches are not revisited: they are applied once at load
	// and their ini keys still need a restart.
	if (g_reloadCountdown-- == 0) {
		g_reloadCountdown = 60;

		if (config::ReloadIfChanged())
			log::Print("Reloaded %s", paths::Ini());
	}

	g_inAir = state != kState_OnGround;

	if (state == kState_OnGround)
		g_airSeconds = 0.f;
	else
		g_airSeconds += deltaTime;

	// Blocked: asked to travel and barely moved. move.input's length is the
	// speed the engine means the player to have, so the comparison needs no
	// threshold pulled out of the air.
	if (const auto *pc = PlayerCharacter::GetSingleton(); pc != nullptr && deltaTime > 0.f) {
		const auto pos = pc->position;
		const auto wanted = move->input.XYZ().Length() / kHavokUnitScale * deltaTime;
		const auto moved = g_haveLastPos
			? NiVector3(pos.x - g_lastPos.x, pos.y - g_lastPos.y, 0.f).Length()
			: wanted;

		g_lastPos = pos;
		g_haveLastPos = true;

		if (wanted > 0.05f && state == kState_OnGround && moved < wanted * 0.25f)
			g_blockedSeconds += deltaTime;
		else
			g_blockedSeconds = 0.f;

		// Capped at 1: overshoot on a single frame is measurement noise, not the
		// player outrunning their own orders, and letting it above 1 only makes
		// the number harder to compare against a threshold.
		if (wanted > 0.05f) {
			const auto ratio = moved / wanted;
			g_speedRatio = ratio > 1.f ? 1.f : ratio;
		} else {
			// Not asking to move is not the same as being stopped by something.
			g_speedRatio = 1.f;
		}

		// Reported on the tenth of a second, so silence means the player never
		// counts as blocked rather than the number never being looked at.
		if (const auto tenths = UInt32(g_blockedSeconds * 10.f);
		    tenths != g_lastBlockedTenths) {
			g_lastBlockedTenths = tenths;
			log::Print("blocked %u/10 s  wanted %u moved %u (x100)",
			           tenths, UInt32(wanted * 100.f), UInt32(moved * 100.f));
		}
	}

	// And this half acts. Nothing above here writes to the player.
	if (!ShouldUsePhysics(charCtrl)) {
		callOriginal();
		return;
	}

	const auto before = *velocity;

	*velocity -= move->surfaceVelocity;
	UpdateVelocity(*move, velocity, state, deltaTime);

	// When this is false the engine's own UpdateThrowback is left in place
	// instead, so the push is applied exactly once either way.
	if (UseCustomKnockback(charCtrl))
		ApplyThrowback(charCtrl);

	*velocity += move->surfaceVelocity;

	// A NaN here would wedge the character controller until the cell is
	// reloaded, so back out rather than write one.
	if (!velocity->IsFinite()) {
		*velocity = before;
		return;
	}

	// Prevent ground state from restoring Z velocity.
	if (state == kState_OnGround)
		move->velocity.z = velocity->z;
}

// The call sites pass `move` and `velocity` on the stack and keep the
// controller in esi.
static __declspec(naked) void hook_MoveCharacter_wrapper()
{
	__asm {
		push [esp+8]
		push [esp+8]
		push esi
		call hook_MoveCharacter
		add  esp, 12
		ret
	}
}

static int __fastcall hook_CheckJumpButton(OSInputGlobals *input, int, int key,
                                           ControlState state)
{
	const auto original = (int(__thiscall*)(OSInputGlobals*, int, ControlState))
		g_origCheckJumpButton;

	if (original(input, key, kControlState_Pressed)) {
		// Fresh input.
		g_player.usedJumpInput = false;
		return true;
	}

	if (g_player.usedJumpInput) {
		// Already used this input to jump.
		return false;
	}

	return original(input, key, kControlState_Held);
}

static bool WillJump(const bhkCharacterController *charCtrl)
{
	// Check that we won't exit jump state early without setting velocity.
	switch (charCtrl->wantState) {
	case kState_OnGround:
	case kState_Climbing:
		return false;
	default:
		return true;
	}
}

static void __fastcall hook_JumpingUpdateVelocity(void *state, int,
                                                  bhkCharacterController *charCtrl)
{
	const auto original = (void(__thiscall*)(void*, bhkCharacterController*))
		g_origJumpingUpdate;

	if (!ShouldUsePhysics(charCtrl) || !WillJump(charCtrl)) {
		original(state, charCtrl);
		return;
	}

	// Must repress jump input.
	g_player.usedJumpInput = true;

	// Additive jumps.
	const auto startZ = charCtrl->velocity.z;
	original(state, charCtrl);

	if (startZ > 0.f)
		charCtrl->velocity.z += startZ;
}

static bool WillFall(const bhkCharacterController *charCtrl)
{
	return !charCtrl->HasSupport() && charCtrl->bFakeSupport == 0;
}

static void ApplyLandingPenalty(bhkCharacterController *charCtrl)
{
	const auto scale = config::g_settings.landingPenaltyImpactSpeed50;

	if (scale <= 0.f)
		return;

	const auto delta = (charCtrl->velocity - g_player.airVelocity).XYZ().Length();
	charCtrl->velocity *= Exp2(-delta / scale);
}

static void __fastcall hook_OnGroundUpdateVelocity(void *state, int,
                                                   bhkCharacterController *charCtrl)
{
	const auto original = (void(__thiscall*)(void*, bhkCharacterController*))
		g_origOnGroundUpdate;

	const auto usePhysics = ShouldUsePhysics(charCtrl);

	// Preserve downward velocity when walking off things. For every other
	// character this restores what the patched-out constructor flag used to do.
	if (WillFall(charCtrl) && (!usePhysics || charCtrl->velocity.z > 0.f))
		charCtrl->velocity.z = 0.f;

	original(state, charCtrl);

	if (!IsPlayerController(charCtrl) || !g_player.JustLanded())
		return;

	g_player.landed = false;

	if (usePhysics)
		ApplyLandingPenalty(charCtrl);
}

static void __fastcall hook_InAirUpdateVelocity(void *state, int,
                                                bhkCharacterController *charCtrl)
{
	const auto original = (void(__thiscall*)(void*, bhkCharacterController*))
		g_origInAirUpdate;

	original(state, charCtrl);

	if (!IsPlayerController(charCtrl))
		return;

	if (charCtrl->hkState == kState_OnGround) {
		g_player.landed     = true;
		g_player.landedTick = g_player.tick;
	} else {
		g_player.airVelocity = charCtrl->velocity;
	}
}

static void __fastcall hook_UpdateCharacterState(bhkCharacterController *charCtrl, int,
                                                 const void *params)
{
	const auto original = (void(__thiscall*)(bhkCharacterController*, const void*))
		g_origUpdateCharacterState;

	if (IsPlayerController(charCtrl)) {
		g_player.tick++;

		if (!g_player.gravityCaptured) {
			g_player.vanillaGravityMult = charCtrl->gravityMult;
			g_player.gravityCaptured = true;
		}

		// Only override while the plugin is actually driving the player;
		// otherwise hand the engine its own value back.
		//
		// Swimming is the one case where the plugin keeps the player and gives
		// the gravity back anyway. This plugin runs the player at roughly
		// double gravity, which on the ground is the point and in water is
		// simply sinking -- swimming up cannot outpace it. Standing the whole
		// plugin down for water fixed that too, but by swapping the movement
		// model at every shoreline, which is felt. Handing back only the number
		// that was actually wrong leaves the boundary seamless.
		const auto own = ShouldUsePhysics(charCtrl);

		charCtrl->gravityMult = own && !IsSwimming(charCtrl)
			? config::g_settings.gravityMult
			: g_player.vanillaGravityMult;
	}

	original(charCtrl, params);
}

static float __fastcall hook_GetFallDistance(bhkCharacterController *charCtrl)
{
	// Prevent fake midair landing.
	if (ShouldUsePhysics(charCtrl))
		return 1.f;

	return ((float(__thiscall*)(bhkCharacterController*))g_origGetFallDistance)(charCtrl);
}

static void __fastcall hook_UpdateThrowback(bhkCharacterController *charCtrl)
{
	// Handled in ApplyThrowback instead, unless that is standing down.
	if (!UseCustomKnockback(charCtrl))
		((void(__thiscall*)(bhkCharacterController*))g_origUpdateThrowback)(charCtrl);
}

// Mid-function hook at 0xC73AC9, where ebx holds the controller. Falls through
// to the original `cmp byte ptr [esp+0x1B], 0` unless the plugin owns the
// player, in which case it jumps past the rooting branch.
//
// eax is preserved across the call: the original code path does not need it,
// but relying on that was an unnecessary risk.
static __declspec(naked) void hook_CheckToRootCharacter()
{
	__asm {
		push eax
		push ecx
		push edx
		push ebx
		call ShouldUsePhysics
		add  esp, 4
		test al, al
		pop  edx
		pop  ecx
		pop  eax
		jne  skip
		// Overwritten instruction, then continue.
		cmp  byte ptr [esp+0x1B], 0
		push 0xC73ACE
		ret
	skip:
		push 0xC73C0D
		ret
	}
}

//----------------------------------------------------------------------------
// Installation
//----------------------------------------------------------------------------

// One original is kept per hooked function, but some are reached from several
// call sites. If another plugin detoured those sites to different places we
// would chain the wrong way round, so say so rather than misbehave silently.
static void ExpectSameOriginal(const char *name, void *first, void *other)
{
	if (first != other)
		log::Print("  WARNING %s: call sites disagree (%x vs %x); "
		           "another plugin hooked them inconsistently",
		           name, (UInt32)first, (UInt32)other);
}

static bool InstallHooks()
{
	// Redirect the three MoveCharacter call sites.
	g_origMoveCharacter =
		patch::CallRel32("MoveCharacter#1", 0xCD414D, 0xD6AEF0, AsCodePtr(hook_MoveCharacter_wrapper));
	ExpectSameOriginal("MoveCharacter", g_origMoveCharacter,
		patch::CallRel32("MoveCharacter#2", 0xCD45D0, 0xD6AEF0, AsCodePtr(hook_MoveCharacter_wrapper)));
	ExpectSameOriginal("MoveCharacter", g_origMoveCharacter,
		patch::CallRel32("MoveCharacter#3", 0xCD4A2A, 0xD6AEF0, AsCodePtr(hook_MoveCharacter_wrapper)));

	g_origCheckJumpButton =
		patch::CallRel32("CheckJumpButton", 0x94215F, 0xA24660, AsCodePtr(hook_CheckJumpButton));

	g_origGetFallDistance =
		patch::CallRel32("GetFallDistance", 0xCD400B, 0xC70550, AsCodePtr(hook_GetFallDistance));

	g_origUpdateThrowback =
		patch::CallRel32("UpdateThrowback#1", 0xCD47AB, 0xC6D5C0, AsCodePtr(hook_UpdateThrowback));
	ExpectSameOriginal("UpdateThrowback", g_origUpdateThrowback,
		patch::CallRel32("UpdateThrowback#2", 0xCD4AA2, 0xC6D5C0, AsCodePtr(hook_UpdateThrowback)));

	g_origJumpingUpdate = patch::Vtable(
		address::kVtbl_bhkCharacterStateJumping, 8, AsCodePtr(hook_JumpingUpdateVelocity));
	g_origOnGroundUpdate = patch::Vtable(
		address::kVtbl_bhkCharacterStateOnGround, 8, AsCodePtr(hook_OnGroundUpdateVelocity));
	g_origInAirUpdate = patch::Vtable(
		address::kVtbl_bhkCharacterStateInAir, 8, AsCodePtr(hook_InAirUpdateVelocity));
	g_origUpdateCharacterState = patch::Vtable(
		address::kVtbl_bhkCharacterController, 50, AsCodePtr(hook_UpdateCharacterState));

	// jmp rel32 over `cmp byte ptr [esp+0x1B], 0`.
	{
		const char expected[] = "\x80\x7C\x24\x1B\x00";
		char jump[] = "\xE9\x00\x00\x00\x00";
		const auto rel = (SInt32)((UInt32)AsCodePtr(hook_CheckToRootCharacter) - (0xC73AC9 + 5));
		*(SInt32*)(jump + 1) = rel;
		patch::Verified("CheckToRootCharacter", 0xC73AC9, expected, jump);
	}

	// bhkCharacterStateOnGround::clearZVelocityOnFall = false, re-implemented
	// per character in hook_OnGroundUpdateVelocity.
	patch::Verified("clearZVelocityOnFall", 0xCD47F1,
	                "\x88\x48\x08\xC3\xCC", "\xC6\x40\x08\x00\xC3");

	const auto &settings = config::g_settings;

	// Don't zero Z velocity with no input on ground (jz -> jmp).
	if (settings.keepGroundZVelocity)
		patch::Verified("keepGroundZVelocity", 0xC7386A, "\x74", "\xEB");

	// Allow jumping while aiming (jz -> jmp).
	if (settings.allowJumpWhileAiming)
		patch::Verified("allowJumpWhileAiming", 0x9422AA, "\x74", "\xEB");

	// Use standard ground collision when not giving input, and don't factor
	// speedPct into ground collisions (jnz -> jmp).
	if (settings.bunnyhopGroundCollision) {
		patch::Verified("groundCollision#1", 0xC72025, "\x75", "\xEB");
		patch::Verified("groundCollision#2", 0xC7203A, "\x75", "\xEB");
	}

	return patch::FailureCount() == 0;
}

static void MessageHandler(NVSEMessagingInterface::Message *msg)
{
	switch (msg->type) {
	case NVSEMessagingInterface::kMessage_PreLoadGame:
	case NVSEMessagingInterface::kMessage_PostLoadGame:
	case NVSEMessagingInterface::kMessage_NewGame:
	case NVSEMessagingInterface::kMessage_ExitToMainMenu:
		// Air velocity and jump latching are per-session state; carrying them
		// across a load produced a phantom landing penalty on the first step.
		g_player.Reset();
		break;
	}
}

//----------------------------------------------------------------------------
// NVSE entry points
//----------------------------------------------------------------------------

extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface *nvse,
                                                       PluginInfo *info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name        = "Player Physics";
	info->version     = 3;

	if (nvse->isEditor)
		return false;

	paths::Init(nvse->GetRuntimeDirectory());
	log::Open();

	if (nvse->runtimeVersion != kRuntimeVersion_1_4_0_525 &&
	    nvse->runtimeVersion != kRuntimeVersion_1_4_0_525ng) {
		// Every address in this plugin is hardcoded for 1.4.0.525. Writing
		// them into a different executable would patch arbitrary code.
		log::Print("Unsupported runtime version %x, expected %x or %x. Not loading.",
		           nvse->runtimeVersion, kRuntimeVersion_1_4_0_525,
		           kRuntimeVersion_1_4_0_525ng);
		log::Close();
		return false;
	}

	return true;
}

extern "C" __declspec(dllexport) bool NVSEPlugin_Load(NVSEInterface *nvse)
{
	config::Load();

	// Registered before the disabled check and before the hooks: the script
	// that drives them has to compile whatever this plugin decides to do, and a
	// missing command is a compile error rather than a quiet false.
	nvse->SetOpcodeBase(kOpcodeBase);

	if (nvse->RegisterCommand(&g_cmdScriptLayerReady) &&
	    nvse->RegisterCommand(&g_cmdBeginInteraction) &&
	    nvse->RegisterCommand(&g_cmdEndInteraction) &&
	    nvse->RegisterCommand(&g_cmdBlockedTime) &&
	    nvse->RegisterCommand(&g_cmdInAir) &&
	    nvse->RegisterCommand(&g_cmdSpeedRatio) &&
	    nvse->RegisterCommand(&g_cmdAirTime))
		log::Print("Registered the script commands from opcode %x.", kOpcodeBase);
	else
		log::Print("Could not register the script commands; falling back to the "
		           "special idle test.");

	if (!config::g_settings.enabled) {
		log::Print("Disabled via bEnabled=0.");
		log::Close();
		return true;
	}

	log::Print("Config: %s", paths::Ini());
	log::Print("Installing hooks.");

	if (!InstallHooks())
		log::Print("%u patch(es) were skipped -- check for a conflicting plugin.",
		           patch::FailureCount());
	else
		log::Print("All hooks installed.");

	if (auto *messaging = (NVSEMessagingInterface*)nvse->QueryInterface(kInterface_Messaging))
		messaging->RegisterListener(nvse->GetPluginHandle(), "NVSE", MessageHandler);
	else
		log::Print("Messaging interface unavailable; state will not reset on load.");

	return true;
}
