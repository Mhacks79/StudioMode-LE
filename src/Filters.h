// Filters.h - the Filters tab.
//
// Colour grading is Photo Mode LE's own pixel shader, run on the back buffer
// at Present before the photo is copied and before the interface is drawn:
// the photo gets it, the interface does not. It works the same with or
// without ENB. Image space modifiers are the game's own (imod / rimod).
#pragma once

struct IDirect3DDevice9;

namespace Filters
{
	// render thread
	void	Apply(IDirect3DDevice9 * device);	// no-op while every setting is neutral
	void	OnDeviceLost();
	void	Draw();

	// input thread
	void	Update();							// starts / stops the chosen image space modifier
	void	OnDeactivate();
	void	RequestReset();
}
