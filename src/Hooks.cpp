// Hooks.cpp
//
// Three entry points from the engine, each behind a fault guard
// (rules/FATAL.md F4): the input dispatch, the free camera's rotation and the
// D3D9 device's Present/Reset. Each keeps whatever target was there before it
// (another plugin's included) and calls it.

#include "Hooks.h"
#include "Offsets.h"
#include "Engine.h"
#include "InputEvents.h"
#include "PhotoMode.h"
#include "Log.h"

#include "skse/SafeWrite.h"

#include <windows.h>
#include <d3d9.h>

#ifdef __GNUC__
	// syntax checks with MinGW only; the shipped build is MSVC
	#define GUARD_TRY		if(true)
	#define GUARD_EXCEPT(f)	else
#else
	#define GUARD_TRY		__try
	#define GUARD_EXCEPT(f)	__except(f)
#endif

namespace
{
	const UInt32 kMaxFaults = 3;

	UInt32	s_inputFaults = 0;
	UInt32	s_cameraFaults = 0;
	UInt32	s_renderFaults = 0;

	int Report(const char * where, EXCEPTION_POINTERS * info, UInt32 & faults)
	{
		_ERROR("%s: exception %08X at %08X (reading %08X); fault %u of %u",
			where, info->ExceptionRecord->ExceptionCode, (UInt32)info->ExceptionRecord->ExceptionAddress,
			info->ExceptionRecord->NumberParameters > 1 ? (UInt32)info->ExceptionRecord->ExceptionInformation[1] : 0,
			faults + 1, kMaxFaults);
		(void)where; (void)info;
		faults++;
		return EXCEPTION_EXECUTE_HANDLER;
	}

	UInt32 CallTarget(UInt32 site)
	{
		return site + 5 + *(SInt32 *)(site + 1);
	}

	// --- input -------------------------------------------------------------

	typedef void (__thiscall * _SendEvent)(void * dispatcher, Input::Event ** head);
	_SendEvent s_sendEvent = NULL;

	Input::Event * s_empty = NULL;

	void DispatchFiltered(void * dispatcher, Input::Event ** head)
	{
		// The poll rebuilds the list every frame and clears it afterwards by
		// zeroing its counters (0x00A698A0); it never walks the links. We still
		// put every link back once the sinks have run.
		Input::Event *	keep[128];
		Input::Event *	savedNext[128];
		UInt32			count = 0;

		Input::Event * first = NULL;
		Input::Event * last = NULL;
		bool overflow = false;

		for(Input::Event * e = *head; e; e = e->next)
		{
			if(count == 128) { overflow = true; break; }
			if(!PhotoMode::ShouldPass(e)) continue;

			keep[count] = e;
			savedNext[count] = e->next;
			count++;

			if(last) last->next = e; else first = e;
			last = e;
		}

		if(overflow)
		{
			// never seen in practice; drop everything rather than half a list
			for(UInt32 i = 0; i < count; i++) keep[i]->next = savedNext[i];
			s_sendEvent(dispatcher, &s_empty);
			return;
		}

		if(last) last->next = NULL;

		s_sendEvent(dispatcher, first ? &first : &s_empty);

		for(UInt32 i = 0; i < count; i++) keep[i]->next = savedNext[i];
	}

	bool InputBody(void * dispatcher, Input::Event ** head)
	{
		if(!Hooks::IsRendererInstalled())
			Hooks::InstallRenderer();

		if(!PhotoMode::OnInput(head ? *head : NULL))
			return false;

		DispatchFiltered(dispatcher, head);
		return true;
	}

	void __fastcall SendInputEvents_Hook(void * dispatcher, void * /*edx*/, Input::Event ** head)
	{
		if(s_inputFaults < kMaxFaults)
		{
			bool handled = false;
			GUARD_TRY { handled = InputBody(dispatcher, head); }
			GUARD_EXCEPT(Report("input", GetExceptionInformation(), s_inputFaults))
			{
				PhotoMode::OnFault();
				handled = false;
			}
			if(handled) return;
		}

		s_sendEvent(dispatcher, head);
	}

	// --- camera ------------------------------------------------------------

	typedef void (__thiscall * _FromEulerAnglesZXY)(void * matrix, float z, float x, float y);
	_FromEulerAnglesZXY s_fromEuler = NULL;

	void __fastcall FreeCameraEuler_Hook(void * matrix, void * /*edx*/, float z, float x, float y)
	{
		float roll = y;
		if(s_cameraFaults < kMaxFaults)
		{
			GUARD_TRY { roll = PhotoMode::GetViewRoll(y); }
			GUARD_EXCEPT(Report("camera", GetExceptionInformation(), s_cameraFaults)) { roll = y; }
		}
		s_fromEuler(matrix, z, x, roll);
	}

	// --- renderer ----------------------------------------------------------

	typedef HRESULT (__stdcall * _Present)(IDirect3DDevice9 * device, const RECT * src, const RECT * dst, HWND window, const RGNDATA * dirty);
	typedef HRESULT (__stdcall * _Reset)(IDirect3DDevice9 * device, D3DPRESENT_PARAMETERS * params);

	enum { kSlotReset = 16, kSlotPresent = 17 };

	_Present	s_present = NULL;
	_Reset		s_reset = NULL;
	bool		s_rendererInstalled = false;
	UInt32		s_rendererAttempts = 0;

	HRESULT __stdcall Present_Hook(IDirect3DDevice9 * device, const RECT * src, const RECT * dst, HWND window, const RGNDATA * dirty)
	{
		if(s_renderFaults < kMaxFaults)
		{
			GUARD_TRY { PhotoMode::OnPresent(device); }
			GUARD_EXCEPT(Report("present", GetExceptionInformation(), s_renderFaults)) { PhotoMode::OnFault(); }
		}
		return s_present(device, src, dst, window, dirty);
	}

	HRESULT __stdcall Reset_Hook(IDirect3DDevice9 * device, D3DPRESENT_PARAMETERS * params)
	{
		GUARD_TRY { PhotoMode::OnDeviceLost(); }
		GUARD_EXCEPT(Report("reset", GetExceptionInformation(), s_renderFaults)) {}

		const HRESULT hr = s_reset(device, params);

		GUARD_TRY { PhotoMode::OnDeviceReset(device); }
		GUARD_EXCEPT(Report("reset", GetExceptionInformation(), s_renderFaults)) {}
		return hr;
	}
}

bool Hooks::InstallInput()
{
	s_sendEvent = (_SendEvent)CallTarget(Addr::kInputSendEventCall);
	WriteRelCall(Addr::kInputSendEventCall, (UInt32)&SendInputEvents_Hook);
	_MESSAGE("hooks: input dispatch at %08X, previous target %08X", Addr::kInputSendEventCall, (UInt32)s_sendEvent);
	return true;
}

bool Hooks::InstallCamera()
{
	s_fromEuler = (_FromEulerAnglesZXY)CallTarget(Addr::kFreeCameraEulerCall);
	WriteRelCall(Addr::kFreeCameraEulerCall, (UInt32)&FreeCameraEuler_Hook);
	_MESSAGE("hooks: free camera rotation at %08X, previous target %08X", Addr::kFreeCameraEulerCall, (UInt32)s_fromEuler);
	return true;
}

bool Hooks::IsRendererInstalled()
{
	return s_rendererInstalled;
}

bool Hooks::InstallRenderer()
{
	if(s_rendererInstalled) return true;
	if(s_rendererAttempts > 600) return false;	// about ten seconds of frames; logged once below
	s_rendererAttempts++;

	IDirect3DDevice9 * device = (IDirect3DDevice9 *)Engine::GetD3DDevice();
	if(!device)
	{
		if(s_rendererAttempts == 601) _ERROR("hooks: no D3D9 device found, the overlay is disabled");
		return false;
	}

	void ** vtbl = *(void ***)device;
	s_reset = (_Reset)vtbl[kSlotReset];
	s_present = (_Present)vtbl[kSlotPresent];

	DWORD old;
	if(!VirtualProtect(&vtbl[kSlotReset], sizeof(void *) * 2, PAGE_EXECUTE_READWRITE, &old))
	{
		_ERROR("hooks: cannot unprotect the device vtable at %08X", (UInt32)vtbl);
		s_rendererAttempts = 1000;
		return false;
	}
	vtbl[kSlotReset] = (void *)&Reset_Hook;
	vtbl[kSlotPresent] = (void *)&Present_Hook;
	VirtualProtect(&vtbl[kSlotReset], sizeof(void *) * 2, old, &old);
	FlushInstructionCache(GetCurrentProcess(), &vtbl[kSlotReset], sizeof(void *) * 2);

	s_rendererInstalled = true;
	_MESSAGE("hooks: D3D9 device %08X, vtable %08X, Present %08X, Reset %08X, thread %u",
		(UInt32)device, (UInt32)vtbl, (UInt32)s_present, (UInt32)s_reset, GetCurrentThreadId());
	return true;
}
