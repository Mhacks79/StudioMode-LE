// Character.h - the Character tab: visibility, expressions and poses of the
// player or of the character selected in the console before opening.
#pragma once

namespace Character
{
	// input thread
	void	OnActivate();		// picks the console selection if it is a loaded character, else the player
	void	OnDeactivate();		// gives back everything this tab changed
	void	Update();			// applies what the interface asked for
	void	RequestReset();

	// interface (render thread); only records requests
	void	Draw();
}
