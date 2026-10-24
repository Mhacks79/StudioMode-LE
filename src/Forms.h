// Forms.h - idle, image space modifier, effect shader and visual effect forms,
// with their editor IDs.
//
// LE's DataHandler does not list idles (the SE mod works around the same gap),
// so both kinds are collected as the game hands them their editor ID while it
// loads the data files. That needs a vtable hook installed from SKSEPlugin_Load.
#pragma once

#include <vector>

namespace Forms
{
	struct Entry
	{
		void *			form;
		const char *	editorID;	// owned by the form, which lives as long as the game
	};

	bool	InstallEarly();			// SKSEPlugin_Load, before the data files load
	void	Finish();				// DataLoaded: sort, log the counts

	const std::vector<Entry> &	Idles();		// paired animations ("pa_") left out
	const std::vector<Entry> &	ImageSpaceModifiers();
	const std::vector<Entry> &	EffectShaders();
	const std::vector<Entry> &	VisualEffects();
	void *	FindIdle(const char * editorID);
}
