// Engine.cpp

#include "Engine.h"
#include "Offsets.h"
#include "Verify.h"
#include "Log.h"

namespace
{
	typedef void	(__thiscall * _ToggleFreeCamera)(void * camera, bool freezeTime);
	typedef void	(__thiscall * _InputContext)(void * inputManager, UInt32 context);
	typedef void	(__thiscall * _SetGlobalTimeMultiplier)(void * timer, float value, bool unk);
	typedef void *	(__cdecl * _LookupFormByID)(UInt32 formID);
	typedef void *	(__cdecl * _SkyGetSingleton)();
	typedef void	(__thiscall * _SkyReleaseOverride)(void * sky);
	typedef void	(__thiscall * _QueueForceWeather)(void * queue, void * weather, bool override);

	template <typename T>
	T * Field(void * base, UInt32 offset)
	{
		return (T *)((UInt8 *)base + offset);
	}

	void * Deref(UInt32 address)
	{
		void * p = *(void **)address;
		return Verify::IsReadable(p) ? p : NULL;
	}

	float * GlobalValue(UInt32 formID)
	{
		void * form = ((_LookupFormByID)Addr::kLookupFormByID)(formID);
		if(!Verify::IsReadable(form, Addr::kGlobalValue + 4)) return NULL;
		return Field<float>(form, Addr::kGlobalValue);
	}
}

// --- camera ------------------------------------------------------------------

void * Engine::GetPlayerCamera()
{
	void * camera = Deref(Addr::kPlayerCamera);
	if(!camera || !Verify::IsReadable(camera, Addr::kCameraWorldFOV + 4)) return NULL;
	return camera;
}

bool Engine::IsFreeCameraActive()
{
	void * camera = GetPlayerCamera();
	if(!camera) return false;

	void * current = *Field<void *>(camera, Addr::kCameraCurrentState);
	void * free = *Field<void *>(camera, Addr::kCameraFreeState);
	return free && current == free;
}

bool Engine::CanToggleFreeCamera()
{
	void * camera = GetPlayerCamera();
	if(!camera) return false;

	// the toggle calls virtuals on the current state and switches to cameraStates[3]
	void * current = *Field<void *>(camera, Addr::kCameraCurrentState);
	void * free = *Field<void *>(camera, Addr::kCameraFreeState);
	if(!Verify::IsReadable(current, 0x10) || !Verify::IsReadable(free, 0x2C)) return false;
	if(!Deref(Addr::kInputManager) || !Deref(Addr::kMain)) return false;
	return true;
}

bool Engine::EnterFreeCamera(bool freezeTime)
{
	if(IsFreeCameraActive()) return true;
	if(!CanToggleFreeCamera()) return false;

	void * camera = GetPlayerCamera();
	((_ToggleFreeCamera)Addr::kToggleFreeCamera)(camera, freezeTime);

	if(!IsFreeCameraActive())
	{
		_WARNING("engine: the free camera did not engage");
		return false;
	}

	// same order as the tfc console command
	((_InputContext)Addr::kInputManagerPushContext)(Deref(Addr::kInputManager), Addr::kInputContextFreeCamera);
	return true;
}

bool Engine::LeaveFreeCamera()
{
	if(!IsFreeCameraActive()) return true;
	if(!CanToggleFreeCamera()) return false;

	((_ToggleFreeCamera)Addr::kToggleFreeCamera)(GetPlayerCamera(), false);
	((_InputContext)Addr::kInputManagerPopContext)(Deref(Addr::kInputManager), Addr::kInputContextFreeCamera);
	return !IsFreeCameraActive();
}

float * Engine::WorldFOV()
{
	void * camera = GetPlayerCamera();
	return camera ? Field<float>(camera, Addr::kCameraWorldFOV) : NULL;
}

float * Engine::FreeCameraSpeed()
{
	return (float *)Addr::kFreeCameraSpeed;
}

// --- time --------------------------------------------------------------------

UInt8 * Engine::FreezeTime()
{
	void * main = Deref(Addr::kMain);
	return main ? Field<UInt8>(main, Addr::kMainFreezeTime) : NULL;
}

float Engine::GetGlobalTimeMultiplier()
{
	return *(float *)Addr::kGlobalTimeMultiplier;
}

void Engine::SetGlobalTimeMultiplier(float value)
{
	((_SetGlobalTimeMultiplier)Addr::kSetGlobalTimeMultiplier)((void *)Addr::kBSTimer, value, true);
}

float * Engine::GameHour()
{
	return GlobalValue(Addr::kFormIDGameHour);
}

float * Engine::TimeScale()
{
	return GlobalValue(Addr::kFormIDTimeScale);
}

// --- weather -----------------------------------------------------------------

namespace
{
	void * GetSky()
	{
		void * sky = ((_SkyGetSingleton)Addr::kSkyGetSingleton)();
		return Verify::IsReadable(sky, Addr::kSkyCurrentWeather + 4) ? sky : NULL;
	}

	bool IsWeather(const void * form)
	{
		return Verify::IsReadable(form, Addr::kFormType + 1) &&
			*(const UInt8 *)((const UInt8 *)form + Addr::kFormType) == Addr::kFormTypeWeather;
	}
}

void * Engine::GetCurrentWeather()
{
	void * sky = GetSky();
	if(!sky) return NULL;
	void * weather = *Field<void *>(sky, Addr::kSkyCurrentWeather);
	return IsWeather(weather) ? weather : NULL;
}

void Engine::ForceWeather(void * weather, bool override)
{
	void * queue = Deref(Addr::kTaskQueue);
	if(!queue || !IsWeather(weather)) return;
	((_QueueForceWeather)Addr::kQueueForceWeather)(queue, weather, override);
}

void Engine::ReleaseWeatherOverride()
{
	if(void * sky = GetSky())
		((_SkyReleaseOverride)Addr::kSkyReleaseOverride)(sky);
}

void Engine::CollectWeathers(std::vector<void *> & out)
{
	out.clear();

	void * handler = Deref(Addr::kDataHandler);
	if(!handler || !Verify::IsReadable(handler, Addr::kDataHandlerWeathers + 0xC)) return;

	void ** entries = *Field<void **>(handler, Addr::kDataHandlerWeathers);
	const UInt32 count = *Field<UInt32>(handler, Addr::kDataHandlerWeathers + 8);
	if(!count || count > 0x10000 || !Verify::IsReadable(entries, count * 4)) return;

	for(UInt32 i = 0; i < count; i++)
	{
		if(IsWeather(entries[i]))
			out.push_back(entries[i]);
	}

	if(out.size() != count)
		_WARNING("engine: %u of %u weather array entries were not weathers", count - (UInt32)out.size(), count);
}

UInt32 Engine::GetFormID(const void * form)
{
	return Verify::IsReadable(form, 0x10) ? *(const UInt32 *)((const UInt8 *)form + 0x0C) : 0;
}

// --- interface ---------------------------------------------------------------

bool Engine::IsInMenuMode()
{
	return *(UInt8 *)Addr::kMenuModeFlagA || *(UInt8 *)Addr::kMenuModeFlagB;
}

bool Engine::AreMenusShown()
{
	void * mm = Deref(Addr::kMenuManager);
	return !mm || *Field<UInt8>(mm, Addr::kMenuManagerShowMenus) != 0;
}

void Engine::ShowMenus(bool show)
{
	if(void * mm = Deref(Addr::kMenuManager))
		*Field<UInt8>(mm, Addr::kMenuManagerShowMenus) = show ? 1 : 0;
}

// --- renderer ----------------------------------------------------------------

void * Engine::GetD3DDevice()
{
	if(void * device = Deref(Addr::kD3DDevice))
		return device;

	void * renderer = Deref(Addr::kRenderer);
	if(!renderer || !Verify::IsReadable(renderer, Addr::kRendererDevice + 4)) return NULL;
	void * device = *Field<void *>(renderer, Addr::kRendererDevice);
	return Verify::IsReadable(device) ? device : NULL;
}
