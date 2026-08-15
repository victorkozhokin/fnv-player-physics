#pragma once

// Minimal subset of the xNVSE plugin API.
//
// The upstream PluginAPI.h drags in the whole NVSE source tree; everything a
// physics plugin needs is the handful of declarations below. Layouts must match
// xNVSE exactly -- see xNVSE/nvse/nvse/PluginAPI.h.

#include "game/types.h"

using PluginHandle = UInt32;

inline constexpr PluginHandle kPluginHandle_Invalid = 0xFFFFFFFF;

// MAKE_NEW_VEGAS_VERSION(4, 0, 525)
inline constexpr UInt32 kRuntimeVersion_1_4_0_525   = 0x040020D0;
// Same build, no-DRM ("nogore"/GOG) executable.
inline constexpr UInt32 kRuntimeVersion_1_4_0_525ng = 0x040020D1;

struct PluginInfo {
	enum { kInfoVersion = 1 };

	UInt32      infoVersion;
	const char *name;
	UInt32      version;
};

struct NVSEInterface {
	UInt32       nvseVersion;
	UInt32       runtimeVersion;
	UInt32       editorVersion;
	UInt32       isEditor;
	bool        (*RegisterCommand)(void *info);
	void        (*SetOpcodeBase)(UInt32 opcode);
	void       *(*QueryInterface)(UInt32 id);
	PluginHandle (*GetPluginHandle)();
	bool        (*RegisterTypedCommand)(void *info, UInt32 retnType);
	const char *(*GetRuntimeDirectory)();
	UInt32       isNogore;
	// Further members exist but are not used here.
};

// A script command's parameter, unused here -- the one command this plugin
// registers takes none -- but the pointer has to have a type.
struct ParamInfo {
	const char *typeStr;
	UInt32      typeID;
	UInt32      isOptional;
};

// Layout must match xNVSE's CommandInfo exactly; NVSE writes `opcode` back.
//
// `parse` is null, which is what DEFINE_COMMAND_PLUGIN does: NVSE substitutes
// its own default parser for plugin commands.
struct CommandInfo {
	const char *longName;    // 00
	const char *shortName;   // 04
	UInt32      opcode;      // 08
	const char *helpText;    // 0C
	UInt16      needsParent; // 10
	UInt16      numParams;   // 12
	ParamInfo  *params;      // 14
	void       *execute;     // 18
	void       *parse;       // 1C
	void       *eval;        // 20
	UInt32      flags;       // 24
};

static_assert(sizeof(CommandInfo) == 0x28);

enum {
	kInterface_Serialization = 0,
	kInterface_Console,
	kInterface_Messaging,
};

struct NVSEMessagingInterface {
	struct Message {
		const char *sender;
		UInt32      type;
		UInt32      dataLen;
		void       *data;
	};

	using EventCallback = void (*)(Message *msg);

	enum {
		kMessage_PostLoad = 0,
		kMessage_ExitGame,
		kMessage_ExitToMainMenu,
		kMessage_LoadGame,
		kMessage_SaveGame,
		kMessage_ScriptPrecompile,
		kMessage_PreLoadGame,
		kMessage_ExitGame_Console,
		kMessage_PostLoadGame,
		kMessage_PostPostLoad,
		kMessage_RuntimeScriptError,
		kMessage_DeleteGame,
		kMessage_RenameGame,
		kMessage_RenameNewGame,
		kMessage_NewGame,
	};

	UInt32 version;
	bool (*RegisterListener)(PluginHandle listener, const char *sender, EventCallback handler);
	bool (*Dispatch)(PluginHandle sender, UInt32 messageType, void *data, UInt32 dataLen,
	                 const char *receiver);
};
