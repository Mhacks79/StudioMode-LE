// Log.h - the SDK's logger, compiled out of release builds.
//
// Studio Mode V1 writes no log file. The calls stay in the source for a
// diagnostic build (define STUDIOMODE_LOG), and otherwise expand to nothing:
// no formatting, no file, no cost.
#pragma once

#include "common/IDebugLog.h"

#ifndef STUDIOMODE_LOG
	#define _MESSAGE(...)	((void)0)
	#define _WARNING(...)	((void)0)
	#define _ERROR(...)		((void)0)
#endif
