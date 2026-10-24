// Forms.cpp

#include "Forms.h"
#include "Offsets.h"
#include "Verify.h"
#include "Log.h"

#include <windows.h>
#include <algorithm>
#include <string.h>

namespace
{
	typedef bool (__thiscall * _SetEditorID)(void * form, const char * id);
	typedef const char * (__thiscall * _GetEditorID)(void * form);

	CRITICAL_SECTION	s_lock;
	bool				s_ready = false;
	_SetEditorID		s_idleSet = NULL;
	_SetEditorID		s_imodSet = NULL;
	std::vector<void *>	s_idleForms;
	std::vector<void *>	s_imodForms;
	std::vector<Forms::Entry>	s_idles;
	std::vector<Forms::Entry>	s_imods;

	// these two kinds forget their editor ID, so the hook keeps a copy
	_SetEditorID		s_shaderSet = NULL;
	_SetEditorID		s_effectSet = NULL;
	std::vector<Forms::Entry>	s_shaderNamed;
	std::vector<Forms::Entry>	s_effectNamed;
	std::vector<Forms::Entry>	s_shaders;
	std::vector<Forms::Entry>	s_effects;

	void Remember(std::vector<Forms::Entry> & list, void * form, const char * id)
	{
		if(!id || !id[0]) return;
		Forms::Entry e = { form, _strdup(id) };
		EnterCriticalSection(&s_lock);
		list.push_back(e);
		LeaveCriticalSection(&s_lock);
	}

	bool __fastcall ShaderSetEditorID_Hook(void * form, void * /*edx*/, const char * id)
	{
		Remember(s_shaderNamed, form, id);
		return s_shaderSet(form, id);
	}

	bool __fastcall EffectSetEditorID_Hook(void * form, void * /*edx*/, const char * id)
	{
		Remember(s_effectNamed, form, id);
		return s_effectSet(form, id);
	}

	// copies the entries still alive with the expected vtable and form type, sorted by name
	void BuildNamed(const std::vector<Forms::Entry> & named, UInt32 vtable, UInt8 formType, std::vector<Forms::Entry> & out)
	{
		out.clear();
		for(size_t i = 0; i < named.size(); i++)
		{
			void * form = named[i].form;
			if(!Verify::IsReadable(form, 0x14) || *(UInt32 *)form != vtable || *((UInt8 *)form + 0x12) != formType) continue;
			out.push_back(named[i]);
		}
		struct ByName { bool operator()(const Forms::Entry & a, const Forms::Entry & b) const { return _stricmp(a.editorID, b.editorID) < 0; } };
		std::sort(out.begin(), out.end(), ByName());

		// a plugin overriding a form names it again: keep one entry per form and name
		std::vector<Forms::Entry> unique;
		for(size_t i = 0; i < out.size(); i++)
			if(unique.empty() || unique.back().form != out[i].form || strcmp(unique.back().editorID, out[i].editorID) != 0)
				unique.push_back(out[i]);
		out.swap(unique);
	}

	bool __fastcall IdleSetEditorID_Hook(void * form, void * /*edx*/, const char * id)
	{
		const bool result = s_idleSet(form, id);
		EnterCriticalSection(&s_lock);
		s_idleForms.push_back(form);
		LeaveCriticalSection(&s_lock);
		return result;
	}

	bool __fastcall ImodSetEditorID_Hook(void * form, void * /*edx*/, const char * id)
	{
		const bool result = s_imodSet(form, id);
		EnterCriticalSection(&s_lock);
		s_imodForms.push_back(form);
		LeaveCriticalSection(&s_lock);
		return result;
	}

	// Patch one vtable slot; keep whatever is there (another plugin's hook included)
	// as long as it points into the executable's code or another module.
	bool PatchSlot(UInt32 vtable, UInt32 expected, UInt32 expectedGet, void * hook, _SetEditorID & original)
	{
		// this runs before Verify::Run: prove the table first by its getter
		if(*(UInt32 *)(vtable + Addr::kSlotGetEditorID * 4) != expectedGet)
		{
			_ERROR("forms: vtable %08X is not the one this plugin was built against", vtable);
			return false;
		}

		void ** slot = (void **)(vtable + Addr::kSlotSetEditorID * 4);
		const UInt32 current = (UInt32)*slot;
		if(current != expected)
			_MESSAGE("forms: vtable %08X slot 0x33 is %08X, not %08X; chaining", vtable, current, expected);
		if(!current) return false;

		DWORD old;
		if(!VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
		original = (_SetEditorID)current;
		*slot = hook;
		VirtualProtect(slot, 4, old, &old);
		return true;
	}

	void Build(const std::vector<void *> & forms, UInt32 vtable, bool skipPaired, std::vector<Forms::Entry> & out)
	{
		out.clear();
		for(size_t i = 0; i < forms.size(); i++)
		{
			void * form = forms[i];
			if(!Verify::IsReadable(form, 0x14) || *(UInt32 *)form != vtable) continue;	// replaced since

			const char * id = ((_GetEditorID)(*(void ***)form)[Addr::kSlotGetEditorID])(form);
			if(!id || !id[0]) continue;
			if(skipPaired && _strnicmp(id, "pa_", 3) == 0) continue;

			Forms::Entry e = { form, id };
			out.push_back(e);
		}

		struct ByName { bool operator()(const Forms::Entry & a, const Forms::Entry & b) const { return _stricmp(a.editorID, b.editorID) < 0; } };
		std::sort(out.begin(), out.end(), ByName());

		// a form can be named more than once (a plugin overriding it): keep one
		std::vector<Forms::Entry> unique;
		for(size_t i = 0; i < out.size(); i++)
			if(unique.empty() || unique.back().form != out[i].form || strcmp(unique.back().editorID, out[i].editorID) != 0)
				unique.push_back(out[i]);
		out.swap(unique);
	}
}

bool Forms::InstallEarly()
{
	InitializeCriticalSection(&s_lock);
	s_ready = true;

	// the slot must hold a code address before anything is patched
	const bool idle = PatchSlot(Addr::kIdleVtable, Addr::kIdleSetEditorID, 0x0055DAA0, (void *)&IdleSetEditorID_Hook, s_idleSet);
	const bool imod = PatchSlot(Addr::kImodVtable, Addr::kImodSetEditorID, 0x004B82A0, (void *)&ImodSetEditorID_Hook, s_imodSet);
	const bool shader = PatchSlot(Addr::kEffectShaderVtable, Addr::kDefaultSetEditorID, Addr::kDefaultGetEditorID, (void *)&ShaderSetEditorID_Hook, s_shaderSet);
	const bool effect = PatchSlot(Addr::kVisualEffectVtable, Addr::kDefaultSetEditorID, Addr::kDefaultGetEditorID, (void *)&EffectSetEditorID_Hook, s_effectSet);
	_MESSAGE("forms: editor ID collectors: idles %s, image space modifiers %s, effect shaders %s, visual effects %s",
		idle ? "on" : "OFF", imod ? "on" : "OFF", shader ? "on" : "OFF", effect ? "on" : "OFF");
	return idle || imod || shader || effect;
}

void Forms::Finish()
{
	if(!s_ready) return;
	EnterCriticalSection(&s_lock);
	Build(s_idleForms, Addr::kIdleVtable, true, s_idles);
	Build(s_imodForms, Addr::kImodVtable, false, s_imods);
	BuildNamed(s_shaderNamed, Addr::kEffectShaderVtable, Addr::kFormTypeEffectShader, s_shaders);
	BuildNamed(s_effectNamed, Addr::kVisualEffectVtable, Addr::kFormTypeVisualEffect, s_effects);
	LeaveCriticalSection(&s_lock);
	_MESSAGE("forms: %u idles, %u image space modifiers, %u effect shaders, %u visual effects",
		(UInt32)s_idles.size(), (UInt32)s_imods.size(), (UInt32)s_shaders.size(), (UInt32)s_effects.size());
}

const std::vector<Forms::Entry> & Forms::Idles()
{
	return s_idles;
}

const std::vector<Forms::Entry> & Forms::ImageSpaceModifiers()
{
	return s_imods;
}

const std::vector<Forms::Entry> & Forms::EffectShaders()
{
	return s_shaders;
}

const std::vector<Forms::Entry> & Forms::VisualEffects()
{
	return s_effects;
}

void * Forms::FindIdle(const char * editorID)
{
	for(size_t i = 0; i < s_idles.size(); i++)
		if(_stricmp(s_idles[i].editorID, editorID) == 0) return s_idles[i].form;
	return NULL;
}
