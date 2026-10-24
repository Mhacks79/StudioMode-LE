// Engine.h - thin, validated access to the Skyrim LE state Photo Mode uses.
//
// Every getter re-reads the chain from the singleton and validates each link
// (rules/FATAL.md F2). Nothing returned here should be kept across frames.

#pragma once

#include <vector>

namespace Engine
{
	// camera
	void *	GetPlayerCamera();
	bool	IsFreeCameraActive();
	bool	CanToggleFreeCamera();
	bool	EnterFreeCamera(bool freezeTime);	// tfc [1], then the free camera input context
	bool	LeaveFreeCamera();					// tfc, then pop the input context
	float *	WorldFOV();
	float *	FreeCameraSpeed();

	// time
	UInt8 *	FreezeTime();
	float	GetGlobalTimeMultiplier();
	void	SetGlobalTimeMultiplier(float value);
	float *	GameHour();
	float *	TimeScale();

	// weather
	void *	GetCurrentWeather();
	void	ForceWeather(void * weather, bool override);
	void	ReleaseWeatherOverride();
	void	CollectWeathers(std::vector<void *> & out);
	UInt32	GetFormID(const void * form);

	// interface
	bool	IsInMenuMode();
	bool	AreMenusShown();
	void	ShowMenus(bool show);

	// renderer
	void *	GetD3DDevice();		// IDirect3DDevice9*
}
