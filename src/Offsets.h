// Offsets.h - every Skyrim LE address this plugin touches.
//
// TESV.exe 1.9.32.0.8 (SHA-256 d43f3de4...cbf2), SKSE 1.7.3. Each entry says
// how it was established; docs/RE-NOTES.md has the disassembly excerpts.
// None of these come from the Special Edition Address Library.

#pragma once

namespace Addr
{
	// --- input -------------------------------------------------------------

	// BSInputDeviceManager::Poll (0x00A68F20) ends by handing the frame's event
	// list to its sinks: lea ecx,[esp+14] / push ecx / mov ecx,ebp / call 0x006EBC10.
	// We replace that call. thiscall(dispatcher, InputEvent** head), ret 4.
	const UInt32 kInputSendEventCall      = 0x00A68F86;
	const UInt32 kEventDispatcherSendEvent = 0x006EBC10;

	// InputStringHolder* (SKSE GameInput.h); the user-event names the engine
	// writes into each IDEvent's controlID. LookHandler::CanProcess 0x00771940
	// compares controlID with [holder+0x18] ("Look").
	const UInt32 kInputStringHolder       = 0x01B2E388;

	// InputManager* (SKSE GameInput.cpp, CommonLibLE g_controlMap).
	const UInt32 kInputManager            = 0x012E7458;
	// ToggleFlyCam console handler 0x0053B300 pushes context 0xD after entering
	// the free camera and pops it after leaving. Both thiscall(ctx), ret 4.
	const UInt32 kInputManagerPushContext = 0x00A67260;
	const UInt32 kInputManagerPopContext  = 0x00A674D0;
	const UInt32 kInputContextFreeCamera  = 0x0D;

	// Utility.IsInMenuMode native 0x00918D90: true when either byte is set.
	const UInt32 kMenuModeFlagA           = 0x01B2E85E;
	const UInt32 kMenuModeFlagB           = 0x01B2E85F;

	// --- camera ------------------------------------------------------------

	const UInt32 kPlayerCamera            = 0x012E7288;	// PlayerCamera** (SKSE, CommonLibLE)
	const UInt32 kCameraCurrentState      = 0x20;		// TESCamera::cameraState
	const UInt32 kCameraFreeState         = 0x78;		// cameraStates[3] (0x6C + 3*4)
	const UInt32 kCameraWorldFOV          = 0xAC;		// written by SetCameraFOV 0x0053C060

	// thiscall(PlayerCamera*, bool freezeTime), ret 4. The tfc console handler
	// calls it with its argument; when entering it stores freezeTime in
	// [Main+0x0A], when leaving it clears that byte.
	const UInt32 kToggleFreeCamera        = 0x0083E4C0;

	// FreeCameraState vtable 0x010E31F4 slot 4 (0x0083A530) builds the camera
	// rotation: FromEulerAnglesZXY(matrix, [this+0x28], [this+0x24], 0.0).
	// The third angle is the roll, always zero. We replace that call.
	const UInt32 kFreeCameraEulerCall     = 0x0083A54F;
	const UInt32 kFromEulerAnglesZXY      = 0x00AABC90;	// thiscall(m, z, x, y), ret 0xC

	// Setting "fFreeCameraTranslationSpeed:Camera" lives at 0x012B1EE8
	// (vtable, value, name); FreeCameraState's update reads the value at
	// 0x0083A697 every frame.
	const UInt32 kFreeCameraSpeed         = 0x012B1EEC;

	// --- time --------------------------------------------------------------

	const UInt32 kMain                    = 0x01B2E860;	// Main**
	const UInt32 kMainFreezeTime          = 0x0A;		// byte, see kToggleFreeCamera

	// BSTimer object (not a pointer). SetGlobalTimeMultiplier thiscall(float, bool)
	// ret 8, as called by the sgtm console handler 0x0052E1C0. The current
	// multiplier is read from 0x012C2410.
	const UInt32 kBSTimer                 = 0x01B4ADD0;
	const UInt32 kSetGlobalTimeMultiplier = 0x00A4B150;
	const UInt32 kGlobalTimeMultiplier    = 0x012C2410;

	// cdecl TESForm* (UInt32 formID) - SKSE GameForms.cpp.
	const UInt32 kLookupFormByID          = 0x00451A30;
	const UInt32 kFormIDGameHour          = 0x38;
	const UInt32 kFormIDTimeScale         = 0x3A;
	// TESGlobal::LoadForm 0x00513590 reads FLTV into [this+0x20].
	const UInt32 kGlobalValue             = 0x20;
	const UInt32 kFormType                = 0x12;
	const UInt8  kFormTypeWeather         = 0x36;

	// --- weather -----------------------------------------------------------

	// cdecl Sky* (void); creates the singleton on first use. GetIsCurrentWeather
	// 0x00507F80 reads the current weather from [sky+0x2C].
	const UInt32 kSkyGetSingleton         = 0x0043B920;
	const UInt32 kSkyCurrentWeather       = 0x2C;
	// ReleaseWeatherOverride console handler 0x00526990: thiscall(Sky*), no args.
	const UInt32 kSkyReleaseOverride      = 0x0058F8D0;
	// ForceWeather console handler 0x0051BD00 queues the change:
	// thiscall([0x01B38308], TESWeather*, bool override), ret 8.
	const UInt32 kTaskQueue               = 0x01B38308;
	const UInt32 kQueueForceWeather       = 0x006A3FB0;

	// DataHandler** (SKSE). Its form arrays are tArrays indexed by form type
	// from +0x08 (keywords, type 4, at +0x38), so weather (type 0x36) is at
	// +0x290. Every entry is checked for form type 0x36 before use.
	const UInt32 kDataHandler             = 0x012E2CB8;
	const UInt32 kDataHandlerWeathers     = 0x290;

	// --- interface ---------------------------------------------------------

	// ToggleMenus console handler 0x0053A390 flips this byte.
	const UInt32 kMenuManager             = 0x012E3548;
	const UInt32 kMenuManagerShowMenus    = 0x118;

	// --- renderer ----------------------------------------------------------

	// 0x00CE7D20: mov eax,[0x01B913F0] / mov ecx,[eax+0x38] / mov [0x01BB0928],ecx
	const UInt32 kD3DDevice               = 0x01BB0928;	// IDirect3DDevice9*
	const UInt32 kRenderer                = 0x01B913F0;
	const UInt32 kRendererDevice          = 0x38;

	// --- 0.2: characters -----------------------------------------------------

	const UInt32 kPlayer                  = 0x01B2E8E4;	// PlayerCharacter** (SKSE g_thePlayer)
	const UInt32 kConsoleRefHandle        = 0x01B3E6EC;	// UInt32 (SKSE g_consoleHandle)
	const UInt32 kInvalidRefHandle        = 0x01310630;	// UInt32 (SKSE g_invalidRefHandle)
	// cdecl bool (UInt32 * handle, TESObjectREFR ** out); the reference comes
	// back with a handle reference, released through BSHandleRefObject (+0x14),
	// count in the low 10 bits of +0x18 (SKSE GameReferences.h).
	const UInt32 kLookupREFRByHandle      = 0x004A9180;
	const UInt8  kFormTypeCharacter       = 0x3E;
	const UInt32 kRefHandleCount          = 0x18;

	// Is3DLoaded native 0x00902950: mov edx,[eax+0x1C0]; call edx
	const UInt32 kRefGet3DSlot            = 0x1C0;
	// NiAVObject::m_flags, bit 0 = culled: 117 "or dword [r+0x98],1" and 108
	// "and dword [r+0x98],0xFFFFFFFE" in the executable; SKSE NiObjects.h agrees.
	const UInt32 kNiAVObjectFlags         = 0x98;

	// actor+0x88 -> process+0x04 -> middle+0xE0 -> node+0xDC (MfgFix NG LE,
	// accepted in game). The face data is 0x1B4 bytes, keyframes of 0x18 bytes
	// from +0x10: [1] expression, [2] modifiers, [3] phonemes are the explicit
	// sources; [5], [7], [9] the final values the engine copies them into.
	// +0x0C pending expression transition, +0x1A3/+0x1A4 force-copy edge,
	// +0x1A6 expression override, +0x1A8 recursive face lock (SimpleLock).
	const UInt32 kActorProcess            = 0x88;
	const UInt32 kReleaseExprTransition   = 0x0059D870;	// cdecl(void ** slot, void * pool)
	const UInt32 kExprTransitionPool      = 0x01B1A470;

	// Actor.PlayIdle native 0x008DB270 calls
	// AIProcess::PlayIdle(actor, 0x40, idle, true, false, target) at 0x0070C4B0, ret 0x18.
	const UInt32 kPlayIdle                = 0x0070C4B0;

	// thiscall const char * (TESObjectREFR*) - SKSE GameReferences.h
	const UInt32 kGetReferenceName        = 0x004DE820;

	// SetFormEditorID (slot 0x33) and GetFormEditorID (slot 0x32); both classes
	// keep their EDID (CommonLibLE vtables.csv): idle at +0x34, IMAD at +0x1E4.
	const UInt32 kIdleVtable              = 0x010A5114;
	const UInt32 kImodVtable              = 0x01087484;
	const UInt32 kSlotGetEditorID         = 0x32;
	const UInt32 kSlotSetEditorID         = 0x33;
	const UInt32 kIdleSetEditorID         = 0x0055D990;
	const UInt32 kImodSetEditorID         = 0x004B82C0;

	// --- 0.3: effects on characters -------------------------------------------

	// TESEffectShader and BGSReferenceEffect do not keep their EDID (slot 0x33 is
	// the TESForm default 0x00C8CCA0, slot 0x32 returns ""), so the name handed
	// to slot 0x33 while loading is copied.
	const UInt32 kEffectShaderVtable      = 0x01082834;
	const UInt32 kVisualEffectVtable      = 0x01086924;
	const UInt32 kDefaultSetEditorID      = 0x00C8CCA0;
	const UInt32 kDefaultGetEditorID      = 0x00401AA0;
	const UInt8  kFormTypeEffectShader    = 0x55;	// SKSE kFormType_EffectShader (enum index 85)
	const UInt8  kFormTypeVisualEffect    = 0x39;	// SKSE kFormType_ReferenceEffect (enum index 57)

	// EffectShader.Play native 0x008FAC30 / VisualEffect.Play native 0x008ECEF0:
	// cdecl ApplyEffectShader(ref, shader, float duration, facing, 0, 0, 0, 0) and
	// cdecl ApplyArtObject(ref, art, float duration, facing, bool, bool, 0, 0).
	// A BGSReferenceEffect holds the art object at +0x14, the shader at +0x18 and
	// flags at +0x1C (bit 1 and bit 2 are passed on as the two bools).
	const UInt32 kApplyEffectShader       = 0x004DDE90;
	const UInt32 kApplyArtObject          = 0x00531BF0;
	// EffectShader.Stop 0x008FA930 / VisualEffect.Stop 0x008ECBB0:
	// thiscall ProcessLists::StopEffectShader(ref, shader) / StopArtObject(ref, art), ret 8
	const UInt32 kProcessLists            = 0x012E32E8;
	const UInt32 kStopEffectShader        = 0x00754840;
	const UInt32 kStopArtObject           = 0x00754900;

	// --- 0.2: image space modifiers -----------------------------------------

	// imod / rimod console handlers 0x0051BD70 / 0x0051BDE0:
	// cdecl Trigger(imod, float strength, NiAVObject * target) and cdecl Stop(imod)
	const UInt32 kImodTrigger             = 0x004B5A00;
	const UInt32 kImodStop                = 0x004B5CB0;
}

