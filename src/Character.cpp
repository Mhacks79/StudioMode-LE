// Character.cpp
//
// The interface writes the wanted state under the Photo Mode lock; Update, on
// the input thread, compares it with what was applied and calls the engine.
// The actor is looked up again from its FormID every time, never kept.
//
// Expressions follow Mfg Fix NG LE (accepted in game): explicit sources [1] [2]
// [3] and the final arrays [5] [7] [9] are written under the face's own lock,
// then the force-copy edge (+0x1A3 = 1, +0x1A4 = 0) makes the engine take them.

#include "Character.h"
#include "Forms.h"
#include "Offsets.h"
#include "Verify.h"
#include "Log.h"

#include "imgui.h"

#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <string.h>

namespace
{
	const char * const kExpressions[] =
	{
		"None", "Dialogue anger", "Dialogue fear", "Dialogue happy", "Dialogue sad", "Dialogue surprise",
		"Dialogue puzzled", "Dialogue disgusted", "Mood neutral", "Mood anger", "Mood fear", "Mood happy",
		"Mood sad", "Mood surprise", "Mood puzzled", "Mood disgusted", "Combat anger", "Combat shout",
	};
	const char * const kPhonemes[] =
	{
		"Aah", "Big aah", "BMP", "Ch J Sh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "Ooh Q", "R", "Th", "W",
	};
	const char * const kModifiers[] =
	{
		"Blink left", "Blink right", "Brow down left", "Brow down right", "Brow in left", "Brow in right",
		"Brow up left", "Brow up right", "Look down", "Look left", "Look right", "Look up", "Squint left", "Squint right",
	};

	enum { kNumExpressions = 17, kNumPhonemes = 16, kNumModifiers = 14 };

	struct Face
	{
		int		expression;			// 0 = none, else 1..17
		int		strength;			// percent
		int		phonemes[kNumPhonemes];
		int		modifiers[kNumModifiers];
	};

	// wanted (interface) and applied (engine) state
	UInt32		s_formID = 0;
	char		s_name[128] = "";
	bool		s_isPlayer = true;

	bool		s_visible = true;
	bool		s_visibleApplied = true;
	bool		s_originalCulled = false;

	Face		s_face;
	bool		s_faceDirty = false;
	bool		s_faceEdited = false;

	int			s_idleIndex = -1;
	int			s_idleRequest = -1;
	bool		s_idlePlayed = false;

	bool		s_resetRequest = false;
	char		s_idleFilter[64] = "";

	// effects: requests from the interface, and what is playing on the character
	int					s_shaderRequest = -1;
	int					s_effectRequest = -1;
	bool				s_stopEffects = false;
	std::vector<void *>	s_playedShaders;
	std::vector<void *>	s_playedArt;
	char				s_effectFilter[64] = "";

	typedef void *		(__cdecl * _LookupFormByID)(UInt32 id);
	typedef bool		(__cdecl * _LookupREFRByHandle)(UInt32 * handle, void ** out);
	typedef void *		(__thiscall * _Get3D)(void * ref);
	typedef const char *(__thiscall * _GetName)(void * ref);
	typedef bool		(__thiscall * _PlayIdle)(void * process, void * actor, UInt32 flags, void * idle, bool unk1, bool unk2, void * target);
	typedef void		(__cdecl * _ReleaseTransition)(void ** slot, void * pool);
	typedef void		(__thiscall * _DeleteThis)(void * object);
	typedef void		(__cdecl * _ApplyEffectShader)(void * ref, void * shader, float duration, void * facing, int, int, int, int);
	typedef void		(__cdecl * _ApplyArtObject)(void * ref, void * art, float duration, void * facing, int, int, int, int);
	typedef void		(__thiscall * _StopEffect)(void * processLists, void * ref, void * form);

	template <typename T> T * Field(void * base, UInt32 offset) { return (T *)((UInt8 *)base + offset); }

	void * Link(void * object, UInt32 offset)
	{
		if(!Verify::IsReadable(object, offset + 4)) return NULL;
		void * p = *Field<void *>(object, offset);
		return Verify::IsReadable(p) ? p : NULL;
	}

	bool IsCharacter(void * form)
	{
		return Verify::IsReadable(form, 0x4C) && *Field<UInt8>(form, Addr::kFormType) == Addr::kFormTypeCharacter;
	}

	void * Actor()
	{
		if(!s_formID) return NULL;
		void * form = ((_LookupFormByID)Addr::kLookupFormByID)(s_formID);
		return IsCharacter(form) ? form : NULL;
	}

	void * Root(void * actor)
	{
		void * root = ((_Get3D)(*(void ***)actor)[Addr::kRefGet3DSlot / 4])(actor);
		return Verify::IsReadable(root, Addr::kNiAVObjectFlags + 4) ? root : NULL;
	}

	// --- face --------------------------------------------------------------

	struct Keyframe				// BSFaceGenKeyframeMultiple, 0x18
	{
		void *	vtbl;
		UInt32	type;
		UInt32	unk08;
		float *	values;
		UInt32	count;
		UInt8	isUpdated;
		UInt8	pad[3];
	};

	void * FaceData(void * actor)
	{
		void * process = Link(actor, Addr::kActorProcess);
		void * middle = Link(process, 0x04);
		void * node = Link(middle, 0xE0);
		void * data = Link(node, 0xDC);
		return Verify::IsReadable(data, 0x1B4) ? data : NULL;
	}

	Keyframe * Key(void * data, UInt32 index)
	{
		return Field<Keyframe>(data, 0x10 + index * 0x18);
	}

	bool KeyReady(void * data, UInt32 index, UInt32 count)
	{
		Keyframe * k = Key(data, index);
		return k->count == count && Verify::IsReadable(k->values, count * 4);
	}

	// SimpleLock at +0x1A8, the same recursive lock the engine takes (SKSE GameTypes.cpp)
	void FaceLock(void * data)
	{
		volatile LONG * owner = Field<volatile LONG>(data, 0x1A8);
		UInt32 * count = Field<UInt32>(data, 0x1AC);
		const LONG me = (LONG)GetCurrentThreadId();
		if(*owner == me) { (*count)++; return; }
		UInt32 spin = 0;
		while(InterlockedCompareExchange(owner, me, 0))
			Sleep(++spin > 10000 ? 1 : 0);
		*count = 1;
	}

	void FaceUnlock(void * data)
	{
		volatile LONG * owner = Field<volatile LONG>(data, 0x1A8);
		UInt32 * count = Field<UInt32>(data, 0x1AC);
		if(--(*count) == 0)
			InterlockedCompareExchange(owner, 0, *owner);
	}

	void WriteKey(void * data, UInt32 explicitIndex, UInt32 finalIndex, UInt32 count, const float * values)
	{
		Keyframe * k = Key(data, explicitIndex);
		for(UInt32 i = 0; i < count; i++) k->values[i] = values[i];
		k->isUpdated = 0;

		// the final array has the same length as its source (17 modifiers, of which 14 are used)
		if(KeyReady(data, finalIndex, k->count))
		{
			Keyframe * f = Key(data, finalIndex);
			for(UInt32 i = 0; i < count; i++) f->values[i] = values[i];
			f->isUpdated = 0;
		}
	}

	// writes the whole face; all zero with no expression gives the face back
	bool ApplyFace(void * actor, const Face & face, bool release)
	{
		void * data = FaceData(actor);
		if(!data) return false;

		bool ok = false;
		FaceLock(data);
		if(KeyReady(data, 1, kNumExpressions) && KeyReady(data, 2, 17) && KeyReady(data, 3, kNumPhonemes))
		{
			// the engine may still own an expression transition; free it through its pool
			void ** transition = Field<void *>(data, 0x0C);
			if(*transition)
				((_ReleaseTransition)Addr::kReleaseExprTransition)(transition, (void *)Addr::kExprTransitionPool);

			float expression[kNumExpressions] = { 0 };
			if(face.expression > 0 && face.expression <= kNumExpressions)
				expression[face.expression - 1] = face.strength / 100.0f;

			float modifiers[17] = { 0 };
			for(int i = 0; i < kNumModifiers; i++) modifiers[i] = face.modifiers[i] / 100.0f;

			float phonemes[kNumPhonemes];
			for(int i = 0; i < kNumPhonemes; i++) phonemes[i] = face.phonemes[i] / 100.0f;

			WriteKey(data, 1, 5, kNumExpressions, expression);
			WriteKey(data, 2, 7, kNumModifiers, modifiers);
			WriteKey(data, 3, 9, kNumPhonemes, phonemes);

			*Field<UInt8>(data, 0x1A6) = (!release && face.expression > 0) ? 1 : 0;	// expression override
			*Field<UInt8>(data, 0x1A3) = 1;		// force the copy on the next update
			*Field<UInt8>(data, 0x1A4) = 0;
			ok = true;
		}
		FaceUnlock(data);
		return ok;
	}

	// --- idles --------------------------------------------------------------

	bool PlayIdle(void * actor, void * idle)
	{
		void * process = Link(actor, Addr::kActorProcess);
		if(!process || !Link(process, 0x08) || !Verify::IsReadable(idle, 0x14)) return false;	// the call needs the middle process
		return ((_PlayIdle)Addr::kPlayIdle)(process, actor, 0x40, idle, true, false, NULL);
	}

	void StopIdle(void * actor)
	{
		void * reset = Forms::FindIdle("ResetRoot");
		if(reset) PlayIdle(actor, reset);
		else _WARNING("character: no ResetRoot idle found, the pose stays");
	}

	// --- effects ------------------------------------------------------------

	// the same calls as EffectShader.Play / VisualEffect.Play with a duration
	// of -1 (until stopped), and as their Stop natives
	void PlayShader(void * actor, void * shader)
	{
		if(!Verify::IsReadable(shader, 0x14)) return;
		((_ApplyEffectShader)Addr::kApplyEffectShader)(actor, shader, -1.0f, NULL, 0, 0, 0, 0);
		s_playedShaders.push_back(shader);
	}

	void PlayVisualEffect(void * actor, void * effect)
	{
		if(!Verify::IsReadable(effect, 0x20)) return;
		void * art = *Field<void *>(effect, 0x14);
		void * shader = *Field<void *>(effect, 0x18);
		const UInt32 flags = *Field<UInt32>(effect, 0x1C);

		if(Verify::IsReadable(art, 0x14))
		{
			((_ApplyArtObject)Addr::kApplyArtObject)(actor, art, -1.0f, NULL, (flags >> 1) & 1, (flags >> 2) & 1, 0, 0);
			s_playedArt.push_back(art);
		}
		if(Verify::IsReadable(shader, 0x14))
			PlayShader(actor, shader);
	}

	void StopEffects(void * actor)
	{
		void * lists = *(void **)Addr::kProcessLists;
		if(actor && Verify::IsReadable(lists, 0x100))
		{
			for(size_t i = 0; i < s_playedShaders.size(); i++)
				((_StopEffect)Addr::kStopEffectShader)(lists, actor, s_playedShaders[i]);
			for(size_t i = 0; i < s_playedArt.size(); i++)
				((_StopEffect)Addr::kStopArtObject)(lists, actor, s_playedArt[i]);
		}
		s_playedShaders.clear();
		s_playedArt.clear();
	}

	// --- target -------------------------------------------------------------

	void * ConsoleSelection()
	{
		UInt32 handle = *(UInt32 *)Addr::kConsoleRefHandle;
		if(!handle || handle == *(UInt32 *)Addr::kInvalidRefHandle) return NULL;

		void * ref = NULL;
		if(!((_LookupREFRByHandle)Addr::kLookupREFRByHandle)(&handle, &ref) || !ref) return NULL;

		// give back the handle reference the lookup took (BSHandleRefObject::DecRefHandle)
		void * handleObject = Field<void>(ref, 0x14);
		volatile LONG * refCount = Field<volatile LONG>(ref, Addr::kRefHandleCount);
		if((InterlockedDecrement(refCount) & 0x3FF) == 0)
			((_DeleteThis)(*(void ***)handleObject)[1])(handleObject);

		return IsCharacter(ref) ? ref : NULL;
	}

	void ResetState()
	{
		memset(&s_face, 0, sizeof(s_face));
		s_face.strength = 100;
		s_faceDirty = s_faceEdited = false;
		s_idleIndex = s_idleRequest = -1;
		s_idlePlayed = false;
		s_visible = s_visibleApplied = true;
		s_shaderRequest = s_effectRequest = -1;
		s_stopEffects = false;
	}

	void Revert(void * actor)
	{
		if(!actor) return;

		if(s_faceEdited)
		{
			Face none;
			memset(&none, 0, sizeof(none));
			ApplyFace(actor, none, true);
		}
		if(s_idlePlayed) StopIdle(actor);
		StopEffects(actor);
		if(s_visibleApplied != !s_originalCulled)
		{
			if(void * root = Root(actor))
			{
				UInt32 & flags = *Field<UInt32>(root, Addr::kNiAVObjectFlags);
				flags = s_originalCulled ? (flags | 1) : (flags & ~1u);
			}
		}
	}

	bool SliderInt(const char * label, int * value, int lo, int hi)
	{
		return ImGui::SliderInt(label, value, lo, hi, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
	}
}

void Character::OnActivate()
{
	ResetState();
	s_resetRequest = false;

	void * actor = ConsoleSelection();
	void * player = *(void **)Addr::kPlayer;
	if(!actor || !Root(actor)) actor = IsCharacter(player) ? player : NULL;

	s_formID = actor ? *Field<UInt32>(actor, 0x0C) : 0;
	s_isPlayer = actor && actor == player;

	s_name[0] = 0;
	if(actor)
	{
		const char * name = s_isPlayer ? "Player" : ((_GetName)Addr::kGetReferenceName)(actor);

		// game strings are in the system code page (Windows-1252 for French);
		// ImGui draws UTF-8, so "Guerrier-Né" would otherwise lose its accent
		char utf8[96] = "";
		WCHAR wide[96];
		if(name && name[0] && MultiByteToWideChar(CP_ACP, 0, name, -1, wide, 96))
			WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), NULL, NULL);
		sprintf_s(s_name, "%s [%08X]", utf8[0] ? utf8 : "Character", s_formID);

		void * root = Root(actor);
		s_originalCulled = root && (*Field<UInt32>(root, Addr::kNiAVObjectFlags) & 1);
		s_visible = s_visibleApplied = !s_originalCulled;
	}
	_MESSAGE("character: %s", actor ? s_name : "no character available");
}

void Character::OnDeactivate()
{
	Revert(Actor());
	ResetState();
	s_formID = 0;
}

void Character::RequestReset()
{
	s_resetRequest = true;
}

void Character::Update()
{
	void * actor = Actor();
	if(!actor) return;

	if(s_resetRequest)
	{
		s_resetRequest = false;
		Revert(actor);
		const bool culled = s_originalCulled;
		ResetState();
		s_visible = s_visibleApplied = !culled;
		return;
	}

	if(s_visible != s_visibleApplied)
	{
		if(void * root = Root(actor))
		{
			UInt32 & flags = *Field<UInt32>(root, Addr::kNiAVObjectFlags);
			flags = s_visible ? (flags & ~1u) : (flags | 1);
			s_visibleApplied = s_visible;
		}
	}

	// re-applied every frame once edited: a frozen update can consume the
	// force edge without copying (Mfg Fix NG LE finding)
	if(s_faceDirty || s_faceEdited)
	{
		if(ApplyFace(actor, s_face, false))
			s_faceEdited = true;
		s_faceDirty = false;
	}

	if(s_stopEffects)
	{
		StopEffects(actor);
		s_stopEffects = false;
	}
	if(s_shaderRequest >= 0)
	{
		const std::vector<Forms::Entry> & shaders = Forms::EffectShaders();
		if(s_shaderRequest < (int)shaders.size()) PlayShader(actor, shaders[s_shaderRequest].form);
		s_shaderRequest = -1;
	}
	if(s_effectRequest >= 0)
	{
		const std::vector<Forms::Entry> & effects = Forms::VisualEffects();
		if(s_effectRequest < (int)effects.size()) PlayVisualEffect(actor, effects[s_effectRequest].form);
		s_effectRequest = -1;
	}

	if(s_idleRequest >= 0)
	{
		const std::vector<Forms::Entry> & idles = Forms::Idles();
		if(s_idleRequest < (int)idles.size())
		{
			if(PlayIdle(actor, idles[s_idleRequest].form))
			{
				s_idlePlayed = true;
				s_idleIndex = s_idleRequest;
			}
			else _MESSAGE("character: %s refused idle %s", s_name, idles[s_idleRequest].editorID);
		}
		s_idleRequest = -1;
	}
}

void Character::Draw()
{
	if(!s_formID)
	{
		ImGui::TextDisabled("No character is available.");
		return;
	}

	ImGui::TextUnformatted(s_name);
	if(s_isPlayer)
		ImGui::TextDisabled("Select an NPC in the console before opening Studio Mode to pose it instead.");

	ImGui::Checkbox(s_isPlayer ? "Show player" : "Show character", &s_visible);

	if(ImGui::BeginTabBar("##character"))
	{
		if(ImGui::BeginTabItem("Expressions"))
		{
			if(ImGui::Combo("Expression", &s_face.expression, kExpressions, kNumExpressions + 1))
				s_faceDirty = true;
			ImGui::BeginDisabled(s_face.expression == 0);
			if(SliderInt("Intensity", &s_face.strength, 0, 100)) s_faceDirty = true;
			ImGui::EndDisabled();

			if(ImGui::TreeNode("Phonemes"))
			{
				for(int i = 0; i < kNumPhonemes; i++)
					if(SliderInt(kPhonemes[i], &s_face.phonemes[i], 0, 100)) s_faceDirty = true;
				ImGui::TreePop();
			}
			if(ImGui::TreeNode("Modifiers"))
			{
				for(int i = 0; i < kNumModifiers; i++)
					if(SliderInt(kModifiers[i], &s_face.modifiers[i], 0, 100)) s_faceDirty = true;
				ImGui::TreePop();
			}
			ImGui::EndTabItem();
		}

		if(ImGui::BeginTabItem("Poses"))
		{
			const std::vector<Forms::Entry> & idles = Forms::Idles();
			ImGui::InputTextWithHint("##filter", "Filter idles", s_idleFilter, sizeof(s_idleFilter));

			const char * preview = (s_idleIndex >= 0 && s_idleIndex < (int)idles.size()) ? idles[s_idleIndex].editorID : "None";
			if(ImGui::BeginCombo("Idle", preview, ImGuiComboFlags_HeightLarge))
			{
				for(int i = 0; i < (int)idles.size(); i++)
				{
					if(s_idleFilter[0] && !StrStrIA(idles[i].editorID, s_idleFilter)) continue;
					ImGui::PushID(i);
					if(ImGui::Selectable(idles[i].editorID, i == s_idleIndex))
						s_idleRequest = i;
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			if(idles.empty()) ImGui::TextDisabled("No idles were collected while the game loaded.");
			else ImGui::TextDisabled("%u idles. Some only play in the right situation.", (unsigned)idles.size());
			ImGui::EndTabItem();
		}

		if(ImGui::BeginTabItem("Effects"))
		{
			ImGui::InputTextWithHint("##effectfilter", "Filter effects", s_effectFilter, sizeof(s_effectFilter));

			struct List { const char * label; const std::vector<Forms::Entry> * entries; int * request; };
			const List lists[] =
			{
				{ "Effect shader", &Forms::EffectShaders(), &s_shaderRequest },
				{ "Visual effect", &Forms::VisualEffects(), &s_effectRequest },
			};
			for(int l = 0; l < 2; l++)
			{
				const std::vector<Forms::Entry> & entries = *lists[l].entries;
				if(ImGui::BeginCombo(lists[l].label, "Add...", ImGuiComboFlags_HeightLarge))
				{
					for(int i = 0; i < (int)entries.size(); i++)
					{
						if(s_effectFilter[0] && !StrStrIA(entries[i].editorID, s_effectFilter)) continue;
						ImGui::PushID(l * 100000 + i);
						if(ImGui::Selectable(entries[i].editorID)) *lists[l].request = i;
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
			}

			const unsigned playing = (unsigned)(s_playedShaders.size() + s_playedArt.size());
			ImGui::BeginDisabled(playing == 0);
			if(ImGui::Button("Stop all effects")) s_stopEffects = true;
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("%u playing", playing);
			ImGui::TextDisabled("Effects stack. They are all removed when Studio Mode closes.");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}
