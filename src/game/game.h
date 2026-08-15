#pragma once

// Game structures and addresses for FalloutNV.exe 1.4.0.525 (Steam and GOG).
//
// Only the members this plugin touches are declared; everything else is
// padding. Every offset below was verified against the retail binary, and the
// static_asserts at the bottom of each struct keep the padding honest.

#include "game/types.h"

//----------------------------------------------------------------------------
// Static addresses
//----------------------------------------------------------------------------

namespace address {

inline constexpr UInt32 kPlayerCharacter        = 0x11DEA3C; // PlayerCharacter **
inline constexpr UInt32 kVATSCameraData         = 0x11F2250; // VATSCameraData *

inline constexpr UInt32 kVtbl_bhkCharacterController    = 0x10C49C4;
inline constexpr UInt32 kVtbl_bhkCharacterStateOnGround = 0x10CB3F0;
inline constexpr UInt32 kVtbl_bhkCharacterStateInAir    = 0x10CB36C;
inline constexpr UInt32 kVtbl_bhkCharacterStateJumping  = 0x10CB398;

// bool __cdecl IsMovementOverrideAnimGroup(UInt16 animGroupID)
//   The callee masks the group id with 0xFF and rejects 0xFF, so it is safe to
//   call with any value AnimData can hold.
inline constexpr UInt32 kIsMovementOverrideSequence = 0x5F2670;

// GMST fMoveSneakMult. The engine's own speed calculation multiplies the walk
// or run speed by this whenever the actor is sneaking (0x647ECE).
inline constexpr UInt32 kSetting_MoveSneakMult = 0x11CFFF4;

} // namespace address

//----------------------------------------------------------------------------
// Havok character controller
//----------------------------------------------------------------------------

namespace hkpCharacterState {
enum StateType : UInt32 {
	kState_OnGround = 0,
	kState_Jumping,
	kState_InAir,
	kState_Climbing,
	kState_Flying,
	kState_Swimming,
	kState_Projectile,
};
}

namespace bhkCharacterListener {
enum ListenerFlags : UInt32 {
	// Set/cleared at 0xC73A27 / 0xC73A33 by bhkCharacterController::Unk_32.
	kHasSupport      = 0x100,
	kJumping         = 0x400,
	// bhkCharacterController::UpdateThrowback (0xC6D5C0) skips the throwback
	// entirely when either of these is set.
	kNoThrowbackA    = 0x800,
	kNoThrowbackB    = 0x1000,
	kIsSceneComplex  = 0x100000,
	kIsUsingFurniture = 0x8000000,
};
}

// The move parameters the game builds on the stack before calling
// MoveCharacter (0xD6AEF0); see 0xCD4925..0xCD4A2A.
struct CharacterMoveParams {
	float          multiplier;      // 00
	UInt8          pad04[0x0C];     // 04
	AlignedVector4 forward;         // 10
	AlignedVector4 up;              // 20
	AlignedVector4 groundNormal;    // 30
	AlignedVector4 velocity;        // 40
	AlignedVector4 input;           // 50
	float          maxSpeed;        // 60
	UInt8          pad64[0x0C];     // 64
	AlignedVector4 surfaceVelocity; // 70
};

static_assert(sizeof(CharacterMoveParams) == 0x80);
static_assert(__builtin_offsetof(CharacterMoveParams, forward)         == 0x10);
static_assert(__builtin_offsetof(CharacterMoveParams, up)              == 0x20);
static_assert(__builtin_offsetof(CharacterMoveParams, groundNormal)    == 0x30);
static_assert(__builtin_offsetof(CharacterMoveParams, velocity)        == 0x40);
static_assert(__builtin_offsetof(CharacterMoveParams, input)           == 0x50);
static_assert(__builtin_offsetof(CharacterMoveParams, maxSpeed)        == 0x60);
static_assert(__builtin_offsetof(CharacterMoveParams, surfaceVelocity) == 0x70);

struct bhkCharacterController {
	UInt8          pad000[0x3F0];
	UInt32         hkState;           // 3F0  chrContext.hkState
	UInt8          pad3F4[0x20];
	UInt32         listenerFlags;     // 414  chrListener.flags
	UInt8          pad418[0xD0];
	float          deltaTime;         // 4E8  stepInfo.deltaTime
	UInt8          pad4EC[0x04];
	AlignedVector4 velocity;          // 4F0
	AlignedVector4 throwbackVelocity; // 500
	AlignedVector4 direction;         // 510
	UInt32         wantState;         // 520
	float          throwbackTimer;    // 524
	UInt8          pad528[0x24];
	float          gravityMult;       // 54C
	UInt8          pad550[0xC0];
	UInt8          bFakeSupport;      // 610
	UInt8          pad611[0x3F];

	bool HasSupport() const
	{
		return (listenerFlags & bhkCharacterListener::kHasSupport) != 0;
	}

	// Mirrors the flag test at the top of UpdateThrowback (0xC6D5C0).
	bool ReceivesThrowback() const
	{
		constexpr auto kMask =
			bhkCharacterListener::kNoThrowbackA | bhkCharacterListener::kNoThrowbackB;
		return (listenerFlags & kMask) == 0;
	}
};

static_assert(sizeof(bhkCharacterController) == 0x650);
static_assert(__builtin_offsetof(bhkCharacterController, hkState)           == 0x3F0);
static_assert(__builtin_offsetof(bhkCharacterController, listenerFlags)     == 0x414);
static_assert(__builtin_offsetof(bhkCharacterController, deltaTime)         == 0x4E8);
static_assert(__builtin_offsetof(bhkCharacterController, velocity)          == 0x4F0);
static_assert(__builtin_offsetof(bhkCharacterController, throwbackVelocity) == 0x500);
static_assert(__builtin_offsetof(bhkCharacterController, direction)         == 0x510);
static_assert(__builtin_offsetof(bhkCharacterController, wantState)         == 0x520);
static_assert(__builtin_offsetof(bhkCharacterController, throwbackTimer)    == 0x524);
static_assert(__builtin_offsetof(bhkCharacterController, gravityMult)       == 0x54C);
static_assert(__builtin_offsetof(bhkCharacterController, bFakeSupport)      == 0x610);

//----------------------------------------------------------------------------
// Actors and movement
//----------------------------------------------------------------------------

// xNVSE names 0x3C runSpeedMult -- a multiplier over walkSpeed, not a speed of
// its own. This plugin has been reading it as a speed, which is what the log
// caught: the figure fed in as the ceiling for acceleration tracks the player's
// current speed instead of bounding it, and which of the two fields is read
// flips with the walk/run toggle.
struct CachedValues {
	UInt8 pad00[0x38];
	float walkSpeed;    // 38
	float runSpeedMult; // 3C
};

struct BaseProcess {
	UInt8         pad00[0x28];
	UInt8         processLevel; // 28
	UInt8         pad29[0x03];
	CachedValues *cachedValues; // 2C
	UInt8         pad30[0x108];
	bhkCharacterController *charCtrl; // 138 (MiddleHighProcess and above only)
};

static_assert(__builtin_offsetof(BaseProcess, processLevel) == 0x28);
static_assert(__builtin_offsetof(BaseProcess, cachedValues) == 0x2C);
static_assert(__builtin_offsetof(BaseProcess, charCtrl)     == 0x138);

namespace ActorMover {
enum MovementFlags : UInt32 {
	kMoveFlag_Forward    = 0x0001,
	kMoveFlag_Backward   = 0x0002,
	kMoveFlag_Left       = 0x0004,
	kMoveFlag_Right      = 0x0008,
	kMoveFlag_TurnLeft   = 0x0010,
	kMoveFlag_TurnRight  = 0x0020,
	kMoveFlag_IsKeyboard = 0x0040,
	kMoveFlag_Walking    = 0x0100,
	kMoveFlag_Running    = 0x0200,
	kMoveFlag_Sneaking   = 0x0400,
	kMoveFlag_Swimming   = 0x0800,
	kMoveFlag_Jump       = 0x1000,
	kMoveFlag_Flying     = 0x2000,
	kMoveFlag_Fall       = 0x4000,
	kMoveFlag_Slide      = 0x8000,
};
}

struct PlayerMover {
	UInt8  pad00[0x88];
	float  moveVector[3];     // 88  analog move direction (controller only)
	UInt32 pcMovementFlags;   // 94
};

static_assert(__builtin_offsetof(PlayerMover, pcMovementFlags) == 0x94);

struct AnimData {
	enum SequenceTypes {
		kSequence_Idle = 0,
		kSequence_Movement,
		kSequence_LeftArm,
		kSequence_LeftHand,
		kSequence_Weapon,
		kSequence_WeaponUp,
		kSequence_WeaponDown,
		kSequence_SpecialIdle,
	};

	// The engine stores 0xFF, not 0xFFFF, in an unused slot.
	enum { kAnimGroup_None = 0xFF };

	UInt8  pad00[0x4C];
	UInt16 animGroupIDs[8]; // 4C
};

static_assert(__builtin_offsetof(AnimData, animGroupIDs) == 0x4C);

struct PlayerCharacter {
	// pcControlFlags mirrors DisablePlayerControls' arguments, one bit each.
	// Bit 0 is movement: PlayerCharacter::SetControlFlags (0x95F590) branches
	// on it to switch the game's control mode.
	enum ControlFlags : UInt8 {
		kControlFlag_Movement = 0x01,
	};

	UInt8        pad000[0x30];
	NiVector3    position;        // 030
	UInt8        pad03C[0x2C];
	BaseProcess *baseProcess;     // 068
	UInt8        pad06C[0x9C];
	UInt32       lifeState;       // 108
	UInt8        pad10C[0x84];
	PlayerMover *actorMover;      // 190
	UInt8        pad194[0x18];
	UInt32       sitSleepState;   // 1AC
	UInt8        pad1B0[0x4D0];
	UInt8        pcControlFlags;  // 680
	UInt8        pad681[0x0F];
	AnimData    *firstPersonAnim; // 690

	static PlayerCharacter *GetSingleton()
	{
		return *(PlayerCharacter**)address::kPlayerCharacter;
	}

	// TESObjectREFR::GetCharacterController, minus the vtable identity check
	// that only matters for non-actors.
	bhkCharacterController *GetCharacterController() const
	{
		const auto *process = baseProcess;

		if (process == nullptr || process->processLevel > 1)
			return nullptr;

		return process->charCtrl;
	}

	// Virtual call into PlayerCharacter::GetAnimData (0x950A10). Returns null
	// while the player has no 3D loaded, so callers must check.
	AnimData *GetAnimData() const
	{
		using func_t = AnimData *(__thiscall*)(const PlayerCharacter*);
		const auto *vtable = *(func_t**)this;
		return vtable[0x1E4 / 4](this);
	}

	// The third person set, via BaseProcess::GetAnimData (vtable 0x1B8), the
	// way Actor::GetAnimData (0x8B70D0) reaches it. GetAnimData above returns
	// only whichever set is currently in charge, which depends on the camera.
	AnimData *GetThirdPersonAnimData() const
	{
		auto *process = baseProcess;

		if (process == nullptr)
			return nullptr;

		using func_t = AnimData *(__thiscall*)(BaseProcess*);
		const auto *vtable = *(func_t**)process;
		return vtable[0x1B8 / 4](process);
	}
};

static_assert(__builtin_offsetof(PlayerCharacter, baseProcess)    == 0x068);
static_assert(__builtin_offsetof(PlayerCharacter, lifeState)      == 0x108);
static_assert(__builtin_offsetof(PlayerCharacter, actorMover)     == 0x190);
static_assert(__builtin_offsetof(PlayerCharacter, sitSleepState)  == 0x1AC);
static_assert(__builtin_offsetof(PlayerCharacter, pcControlFlags)  == 0x680);
static_assert(__builtin_offsetof(PlayerCharacter, firstPersonAnim) == 0x690);

//----------------------------------------------------------------------------
// Misc singletons
//----------------------------------------------------------------------------

struct VATSCameraData {
	UInt8  pad00[0x08];
	UInt32 mode; // 08

	static VATSCameraData *Get() { return (VATSCameraData*)address::kVATSCameraData; }
};

struct OSInputGlobals;

// Raw DirectInput key state, indexed by scancode. The same block the jump hook
// above is handed, reached here through its global for the slide hotkey.
namespace address {
inline constexpr UInt32 kOSInputGlobals = 0x11F35CC; // OSInputGlobals **
}

struct RawInput {
	UInt8 pad0000[0x18F8];
	UInt8 currKeyStates[256]; // 18F8

	static RawInput *Get() { return *(RawInput**)address::kOSInputGlobals; }
};

static_assert(__builtin_offsetof(RawInput, currKeyStates) == 0x18F8);

// scancode is a DirectInput code (DIK_*), e.g. 0x1D for left control.
inline bool IsKeyDown(UInt32 scancode)
{
	if (scancode == 0 || scancode > 255)
		return false;

	const auto *input = RawInput::Get();
	return input != nullptr && input->currKeyStates[scancode] != 0;
}

// Setting is { vtbl, Info data, const char *name }; the float sits at +4.
inline float GetSettingFloat(UInt32 setting)
{
	return *(const float*)(setting + 4);
}

inline bool IsMovementOverrideSequence(UInt16 animGroupID)
{
	return CdeclCall<bool>(address::kIsMovementOverrideSequence, animGroupID);
}
