// PhotoMode.h - the Photo Mode state machine, its input and its interface.
#pragma once

struct IDirect3DDevice9;

namespace Input { struct Event; }

namespace PhotoMode
{
	// main thread, from the input dispatch, before the game's sinks run.
	// Returns true when the frame's events must be filtered through ShouldPass.
	bool OnInput(Input::Event * head);
	bool ShouldPass(const Input::Event * e);

	// render thread, from IDirect3DDevice9::Present
	void OnPresent(IDirect3DDevice9 * device);
	void OnDeviceLost();
	void OnDeviceReset(IDirect3DDevice9 * device);

	// FreeCameraState's rotation; returns the roll to use
	float GetViewRoll(float fallback);

	// a fault guard fired: give everything back and stand down
	void OnFault();

	// SKSE messages
	void OnDataLoaded();
	void OnLoadGame();

	bool IsActive();
}
