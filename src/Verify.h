// Verify.h - refuse to install on an executable this plugin was not built for.
#pragma once

namespace Verify
{
	// Compares the bytes at every address the plugin calls or patches with
	// what TESV.exe 1.9.32.0.8 has there. Logs each mismatch.
	bool Run();

	// Pointer sanity for engine chains (rules/FATAL.md F2): non-null,
	// 4-aligned, below 0xFFFF0000, committed and readable.
	bool IsReadable(const void * p, UInt32 size = 4);
}
