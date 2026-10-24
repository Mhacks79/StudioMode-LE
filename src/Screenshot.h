// Screenshot.h - PNG captures of the back buffer, without the Studio Mode window.
#pragma once

#include <string>

struct IDirect3DDevice9;

namespace Screenshot
{
	void SetFolder(const std::string & folder);		// empty: My Games\Skyrim\PhotoMode

	// Copies the back buffer now (render thread) and writes the PNG on a
	// worker thread. Returns false if the copy failed; the result of the
	// write arrives later through PollResult.
	bool Capture(IDirect3DDevice9 * device);

	// The last finished write, once: true and the path, or false and an error.
	bool PollResult(bool & ok, std::string & message);
}
