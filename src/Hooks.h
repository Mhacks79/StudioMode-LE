// Hooks.h
#pragma once

namespace Hooks
{
	bool InstallInput();		// main thread, after Verify::Run
	bool InstallCamera();
	bool InstallRenderer();		// needs the D3D9 device; retried until it exists
	bool IsRendererInstalled();
}
