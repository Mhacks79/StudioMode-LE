// PhotoMode.cpp
//
// Studio Mode - Skyrim LE port of Photo Mode by powerofthree:
// https://github.com/powerof3/PhotoMode (GPL-3.0)
//
// Threads: OnInput runs on the main thread (BSInputDeviceManager::Poll), the
// interface on whatever thread presents. Everything shared is guarded by one
// critical section; the engine's free camera is only ever toggled from
// OnInput, exactly where the tfc console command would run.

#include "PhotoMode.h"
#include "Character.h"
#include "Config.h"
#include "Engine.h"
#include "Filters.h"
#include "Icons.h"
#include "InputEvents.h"
#include "Offsets.h"
#include "Screenshot.h"
#include "Verify.h"
#include "Log.h"

#include "imgui.h"
#include "backends/imgui_impl_dx9.h"

#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stdio.h>

#include <string>
#include <vector>

namespace
{
	const float kPi = 3.14159265358979f;
	const UInt32 kKeyEscape = 0x01;

	enum Tab { kTab_Camera, kTab_Time, kTab_Character, kTab_Filters, kTab_Count };
	const char * const kTabNames[kTab_Count] = { "Camera", "Time", "Character", "Filters" };
	const char * const kTabIconNames[kTab_Count] =
	{
		ICON_FA_CAMERA " Camera", ICON_FA_CLOCK " Time", ICON_FA_PERSON " Character", ICON_FA_CIRCLE_HALF_STROKE " Filters",
	};

	// SKSE gamepad key codes (InputMap.h) from the XInput masks in ButtonEvent::keyMask
	enum Pad
	{
		kPad_DpadUp = 266, kPad_DpadDown, kPad_DpadLeft, kPad_DpadRight, kPad_Start, kPad_Back,
		kPad_LeftThumb, kPad_RightThumb, kPad_LB, kPad_RB, kPad_A, kPad_B, kPad_X, kPad_Y, kPad_LT, kPad_RT,
		kPad_Invalid = 0,
	};

	UInt32 PadCode(UInt32 mask)
	{
		switch(mask)
		{
			case 0x0001: return kPad_DpadUp;		case 0x0002: return kPad_DpadDown;
			case 0x0004: return kPad_DpadLeft;		case 0x0008: return kPad_DpadRight;
			case 0x0010: return kPad_Start;			case 0x0020: return kPad_Back;
			case 0x0040: return kPad_LeftThumb;		case 0x0080: return kPad_RightThumb;
			case 0x0100: return kPad_LB;			case 0x0200: return kPad_RB;
			case 0x1000: return kPad_A;				case 0x2000: return kPad_B;
			case 0x4000: return kPad_X;				case 0x8000: return kPad_Y;
			case 0x0009: return kPad_LT;			case 0x000A: return kPad_RT;
			default:     return kPad_Invalid;
		}
	}

	enum Grid { kGrid_None, kGrid_Thirds, kGrid_Golden, kGrid_Diagonals, kGrid_Center, kGrid_Count };
	const char * const kGridNames[kGrid_Count] = { "None", "Rule of thirds", "Golden ratio", "Diagonals", "Center cross" };

	// what the game had before Photo Mode touched it
	struct Original
	{
		float	fov;
		float	speed;
		UInt8	freezeTime;
		float	timeMultiplier;
		float	gameHour;
		float	timeScale;
		void *	weather;
		bool	menusShown;
		bool	wasFreeCamera;
	};

	CRITICAL_SECTION	s_lock;
	bool				s_lockReady = false;
	bool				s_ready = false;		// data loaded and hooks installed
	DWORD				s_inputThread = 0;		// the thread that polls input; the camera is toggled there

	// engine calls the interface asks for, made on the input thread
	// (measured: input, rendering and data loading are three different threads)
	float				s_pendingTimeMult = -1.0f;
	void *				s_pendingWeather = NULL;

	bool				s_active = false;
	bool				s_filterThisFrame = false;
	bool				s_requestExit = false;
	bool				s_uiHidden = false;
	Original			s_original;

	float				s_roll = 0.0f;			// radians
	float				s_timeScaleMult = 1.0f;
	bool				s_hourChanged = false;
	bool				s_weatherForced = false;
	std::vector<void *>	s_weathers;
	int					s_weatherIndex = -1;
	int					s_grid = kGrid_None;
	int					s_tab = kTab_Camera;
	int					s_tabRequest = -1;

	// input
	bool				s_rightMouse = false;
	bool				s_panKey = false;
	float				s_cursorX = -1.0f;
	float				s_cursorY = -1.0f;
	const char *		s_passControls[12];
	UInt32				s_passControlCount = 0;
	const char *		s_lookControl = NULL;
	bool				s_gamepad = false;		// the last input came from a gamepad: no cursor
	bool				s_wantText = false;		// an ImGui text field has the keyboard
	bool				s_shift = false;
	bool				s_ctrl = false;
	bool				s_altGr = false;
	bool				s_focusWindow = false;	// give the window keyboard/gamepad focus on the next frame

	// photos
	int					s_photoCountdown = -1;	// frames until the capture
	bool				s_photoHidHUD = false;

	// feedback
	std::string			s_toast;
	DWORD				s_toastUntil = 0;

	// ImGui
	bool				s_imguiReady = false;
	bool				s_imguiFailed = false;
	LARGE_INTEGER		s_frequency;
	LARGE_INTEGER		s_lastFrame;
	float				s_displayW = 0.0f;
	float				s_displayH = 0.0f;

	struct Lock
	{
		Lock()	{ EnterCriticalSection(&s_lock); }
		~Lock()	{ LeaveCriticalSection(&s_lock); }
	};

	bool IsPanning()
	{
		return s_rightMouse || s_panKey || s_uiHidden;
	}

	void Toast(const std::string & text)
	{
		s_toast = text;
		s_toastUntil = GetTickCount() + 3000;
	}

	// --- original state ----------------------------------------------------

	void CaptureOriginal()
	{
		Original & o = s_original;
		float * fov = Engine::WorldFOV();
		float * speed = Engine::FreeCameraSpeed();
		UInt8 * freeze = Engine::FreezeTime();
		float * hour = Engine::GameHour();
		float * scale = Engine::TimeScale();

		o.fov = fov ? *fov : 0.0f;
		o.speed = *speed;
		o.freezeTime = freeze ? *freeze : 0;
		o.timeMultiplier = Engine::GetGlobalTimeMultiplier();
		o.gameHour = hour ? *hour : -1.0f;
		o.timeScale = scale ? *scale : -1.0f;
		o.weather = Engine::GetCurrentWeather();
		o.menusShown = Engine::AreMenusShown();
		o.wasFreeCamera = Engine::IsFreeCameraActive();
	}

	void RevertCamera()
	{
		if(float * fov = Engine::WorldFOV())
			if(s_original.fov > 0.0f) *fov = s_original.fov;
		*Engine::FreeCameraSpeed() = s_original.speed;
		s_roll = 0.0f;
		s_grid = kGrid_None;
	}

	void RevertTime()
	{
		if(UInt8 * freeze = Engine::FreezeTime())
			*freeze = s_original.freezeTime;
		if(fabsf(Engine::GetGlobalTimeMultiplier() - s_original.timeMultiplier) > 0.0001f)
			Engine::SetGlobalTimeMultiplier(s_original.timeMultiplier);
		if(float * scale = Engine::TimeScale())
			if(s_original.timeScale >= 0.0f && s_timeScaleMult != 1.0f) *scale = s_original.timeScale;
		if(float * hour = Engine::GameHour())
			if(s_original.gameHour >= 0.0f && s_hourChanged) *hour = s_original.gameHour;
		s_timeScaleMult = 1.0f;
		s_hourChanged = false;

		if(s_weatherForced)
		{
			Engine::ReleaseWeatherOverride();
			if(s_original.weather) Engine::ForceWeather(s_original.weather, false);
			s_weatherForced = false;
		}
		s_weatherIndex = -1;
	}

	// --- activation --------------------------------------------------------

	bool CanActivate()
	{
		if(!s_ready) return false;
		if(Engine::IsInMenuMode()) return false;
		return Engine::CanToggleFreeCamera() || Engine::IsFreeCameraActive();
	}

	void Activate()
	{
		CaptureOriginal();

		if(!s_original.wasFreeCamera && !Engine::EnterFreeCamera(Config::FreezeTimeOnStart))
		{
			_WARNING("photo mode: could not enter the free camera");
			return;
		}

		if(Config::FreeCameraSpeed > 0.0f)
			*Engine::FreeCameraSpeed() = Config::FreeCameraSpeed;
		if(Config::FreezeTimeOnStart)
			if(UInt8 * freeze = Engine::FreezeTime()) *freeze = 1;

		Engine::CollectWeathers(s_weathers);

		s_active = true;
		s_uiHidden = false;
		s_requestExit = false;
		s_rightMouse = s_panKey = false;
		s_tab = kTab_Camera;
		s_cursorX = s_cursorY = -1.0f;	// centred on the next frame
		s_wantText = false;
		s_focusWindow = true;
		Character::OnActivate();
		_MESSAGE("photo mode: on (thread %u, %u weathers)", GetCurrentThreadId(), (UInt32)s_weathers.size());
	}

	void Deactivate(bool leaveCamera)
	{
		if(!s_active) return;
		s_active = false;

		if(s_photoCountdown >= 0) s_photoCountdown = -1;
		s_pendingTimeMult = -1.0f;
		s_pendingWeather = NULL;
		RevertCamera();
		RevertTime();
		Character::OnDeactivate();
		Filters::OnDeactivate();

		if(leaveCamera && !s_original.wasFreeCamera)
			Engine::LeaveFreeCamera();

		// LeaveFreeCamera clears the freeze flag; put back what the player had
		if(UInt8 * freeze = Engine::FreezeTime())
			*freeze = s_original.freezeTime;

		Engine::ShowMenus(s_original.menusShown);
		s_uiHidden = false;
		s_rightMouse = s_panKey = false;

		if(s_imguiReady)
		{
			ImGuiIO & io = ImGui::GetIO();
			io.AddMouseButtonEvent(0, false);
			io.AddMouseButtonEvent(1, false);
			io.AddMouseButtonEvent(2, false);
			io.ClearInputKeys();
		}
		s_wantText = false;
		_MESSAGE("photo mode: off");
	}

	void RequestPhoto()
	{
		if(s_photoCountdown >= 0) return;

		if(Config::HideHUDForPhotos && Engine::AreMenusShown())
		{
			Engine::ShowMenus(false);
			s_photoHidHUD = true;
			s_photoCountdown = 2;		// let the HUD disappear from the frame
		}
		else
		{
			s_photoHidHUD = false;
			s_photoCountdown = 0;
		}
	}

	void ResetTab(int tab)
	{
		if(tab == kTab_Camera)
		{
			RevertCamera();
			if(Config::FreeCameraSpeed > 0.0f) *Engine::FreeCameraSpeed() = Config::FreeCameraSpeed;
			Toast("Camera settings reset");
		}
		else if(tab == kTab_Time)
		{
			RevertTime();
			Toast("Time and weather reset");
		}
		else if(tab == kTab_Character)
		{
			Character::RequestReset();
			Toast("Character reset");
		}
		else
		{
			Filters::RequestReset();
			Toast("Filters reset");
		}
	}

	// --- input -------------------------------------------------------------

	void ReadControlNames()
	{
		s_passControlCount = 0;
		s_lookControl = NULL;

		const UInt8 * holder = *(const UInt8 **)Addr::kInputStringHolder;
		if(!Verify::IsReadable(holder, 0x60)) return;

		const UInt32 movement[] = { Input::kControl_Forward, Input::kControl_Back, Input::kControl_StrafeLeft, Input::kControl_StrafeRight, Input::kControl_Move, 0x58 /* Run */ };
		for(UInt32 i = 0; i < sizeof(movement) / sizeof(movement[0]); i++)
			s_passControls[s_passControlCount++] = *(const char **)(holder + movement[i]);

		// FreeCameraState's own controls, from its input handler 0x0083A9B0
		for(UInt32 a = 0x01B3E2E0; a <= 0x01B3E2F0; a += 4)
			s_passControls[s_passControlCount++] = *(const char **)a;

		s_lookControl = *(const char **)(holder + Input::kControl_Look);
	}

	bool IsOurKey(UInt32 key)
	{
		return key == Config::ToggleKey || key == Config::TakePhotoKey || key == Config::ToggleUIKey ||
			key == Config::FreezeTimeKey || key == Config::ResetKey || key == Config::NextTabKey ||
			key == Config::PreviousTabKey || key == kKeyEscape;
	}

	bool IsPassControl(const char * id)
	{
		if(!id) return false;
		for(UInt32 i = 0; i < s_passControlCount; i++)
			if(s_passControls[i] && id == s_passControls[i]) return true;
		return false;
	}

	// While an ImGui text field is active, keys type text instead of driving Photo Mode.
	// The scan code goes through the user's keyboard layout, so AZERTY types as AZERTY.
	void TypeKey(const Input::ButtonEvent * b)
	{
		const UInt32 key = b->keyMask;
		const bool held = Input::IsHeld(b);
		ImGuiIO & io = ImGui::GetIO();

		struct Named { UInt32 key; ImGuiKey imgui; };
		static const Named kNamed[] =
		{
			{ 0x0E, ImGuiKey_Backspace }, { 0xD3, ImGuiKey_Delete }, { 0xCB, ImGuiKey_LeftArrow }, { 0xCD, ImGuiKey_RightArrow },
			{ 0xC7, ImGuiKey_Home }, { 0xCF, ImGuiKey_End }, { 0x1C, ImGuiKey_Enter }, { 0x9C, ImGuiKey_KeypadEnter },
			{ 0x01, ImGuiKey_Escape }, { 0x0F, ImGuiKey_Tab },
		};
		for(UInt32 i = 0; i < sizeof(kNamed) / sizeof(kNamed[0]); i++)
			if(kNamed[i].key == key) { io.AddKeyEvent(kNamed[i].imgui, held); return; }

		if(!Input::IsDown(b)) return;

		HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), NULL));
		const UINT vk = MapVirtualKeyExA(key, MAPVK_VSC_TO_VK, layout);
		if(!vk) return;

		BYTE state[256] = { 0 };
		if(s_shift) state[VK_SHIFT] = 0x80;
		if(s_ctrl || s_altGr) state[VK_CONTROL] = 0x80;
		if(s_altGr) state[VK_MENU] = 0x80;

		WCHAR text[4];
		const int n = ToUnicodeEx(vk, key, state, text, 4, 0, layout);
		for(int i = 0; i < n; i++)
			if(text[i] >= 0x20) io.AddInputCharacterUTF16(text[i]);
	}

	void HandleKey(const Input::ButtonEvent * b)
	{
		const UInt32 key = b->keyMask;

		if(key == 0x2A || key == 0x36) s_shift = Input::IsHeld(b);
		if(key == 0x1D || key == 0x9D) s_ctrl = Input::IsHeld(b);
		if(key == 0xB8) s_altGr = Input::IsHeld(b);

		if(s_wantText && s_imguiReady && !s_uiHidden)
		{
			TypeKey(b);
			return;
		}

		if(key == Config::PanCameraKey)
			s_panKey = Input::IsHeld(b);

		if(!Input::IsDown(b)) return;

		if(key == Config::ToggleKey || key == kKeyEscape)
			s_requestExit = true;
		else if(key == Config::TakePhotoKey)
			RequestPhoto();
		else if(key == Config::ToggleUIKey)
		{
			s_uiHidden = !s_uiHidden;
			Engine::ShowMenus(!s_uiHidden && s_original.menusShown);
		}
		else if(key == Config::FreezeTimeKey)
		{
			if(UInt8 * freeze = Engine::FreezeTime())
			{
				*freeze = *freeze ? 0 : 1;
				Toast(*freeze ? "Time frozen" : "Time running");
			}
		}
		else if(key == Config::ResetKey)
			ResetTab(s_tab);
		else if(key == Config::NextTabKey)
			s_tabRequest = (s_tab + 1) % kTab_Count;
		else if(key == Config::PreviousTabKey)
			s_tabRequest = (s_tab + kTab_Count - 1) % kTab_Count;
	}

	void HandleGamepad(const Input::ButtonEvent * b)
	{
		const UInt32 code = PadCode(b->keyMask);
		if(code == kPad_Invalid) return;
		s_gamepad = true;

		const bool held = Input::IsHeld(b);
		const bool imgui = s_imguiReady && !s_uiHidden;

		// interface navigation: the D-pad moves, A activates, B goes back
		ImGuiKey nav = ImGuiKey_None;
		switch(code)
		{
			case kPad_DpadUp:		nav = ImGuiKey_GamepadDpadUp; break;
			case kPad_DpadDown:		nav = ImGuiKey_GamepadDpadDown; break;
			case kPad_DpadLeft:		nav = ImGuiKey_GamepadDpadLeft; break;
			case kPad_DpadRight:	nav = ImGuiKey_GamepadDpadRight; break;
			case kPad_A:			nav = ImGuiKey_GamepadFaceDown; break;
			case kPad_B:			nav = ImGuiKey_GamepadFaceRight; break;
		}
		if(nav != ImGuiKey_None)
		{
			if(imgui) ImGui::GetIO().AddKeyEvent(nav, held);
			return;
		}

		if(!Input::IsDown(b)) return;

		if(code == Config::GamepadExit)
			s_requestExit = true;
		else if(code == Config::GamepadTakePhoto)
			RequestPhoto();
		else if(code == Config::GamepadToggleUI)
		{
			s_uiHidden = !s_uiHidden;
			Engine::ShowMenus(!s_uiHidden && s_original.menusShown);
		}
		else if(code == Config::GamepadReset)
			ResetTab(s_tab);
		else if(code == Config::GamepadNextTab)
			s_tabRequest = (s_tab + 1) % kTab_Count;
		else if(code == Config::GamepadPreviousTab)
			s_tabRequest = (s_tab + kTab_Count - 1) % kTab_Count;
	}

	void HandleMouse(const Input::Event * e)
	{
		const bool imgui = s_imguiReady && !s_uiHidden;
		if(e->type == Input::kEvent_MouseMove || e->type == Input::kEvent_Button) s_gamepad = false;

		if(e->type == Input::kEvent_Button)
		{
			const Input::ButtonEvent * b = (const Input::ButtonEvent *)e;
			switch(b->keyMask)
			{
				case Input::kMouse_Left:
				case Input::kMouse_Middle:
					if(imgui) ImGui::GetIO().AddMouseButtonEvent(b->keyMask == Input::kMouse_Left ? 0 : 2, Input::IsHeld(b));
					break;
				case Input::kMouse_Right:
					s_rightMouse = Input::IsHeld(b);
					break;
				case Input::kMouse_WheelUp:
				case Input::kMouse_WheelDown:
					if(imgui && Input::IsDown(b)) ImGui::GetIO().AddMouseWheelEvent(0.0f, b->keyMask == Input::kMouse_WheelUp ? 1.0f : -1.0f);
					break;
			}
		}
		else if(e->type == Input::kEvent_MouseMove && imgui && !IsPanning() && s_displayW > 0.0f)
		{
			const Input::MouseMoveEvent * m = (const Input::MouseMoveEvent *)e;
			s_cursorX += m->dx * Config::CursorSpeed;
			s_cursorY += m->dy * Config::CursorSpeed;
			if(s_cursorX < 0.0f) s_cursorX = 0.0f;
			if(s_cursorY < 0.0f) s_cursorY = 0.0f;
			if(s_cursorX > s_displayW - 1.0f) s_cursorX = s_displayW - 1.0f;
			if(s_cursorY > s_displayH - 1.0f) s_cursorY = s_displayH - 1.0f;
			ImGui::GetIO().AddMousePosEvent(s_cursorX, s_cursorY);
		}
	}

	bool ToggleKeyPressed(const Input::Event * head)
	{
		bool padModifier = (Config::GamepadToggleModifier == 0);
		bool padPressed = false;

		for(const Input::Event * e = head; e; e = e->next)
		{
			if(e->type != Input::kEvent_Button) continue;
			const Input::ButtonEvent * b = (const Input::ButtonEvent *)e;

			if(e->device == Input::kDevice_Keyboard && b->keyMask == Config::ToggleKey && Input::IsDown(b))
				return true;

			if(e->device == Input::kDevice_Gamepad && Config::GamepadToggle)
			{
				const UInt32 code = PadCode(b->keyMask);
				if(code == Config::GamepadToggleModifier && Input::IsHeld(b)) padModifier = true;
				if(code == Config::GamepadToggle && Input::IsDown(b)) padPressed = true;
			}
		}
		if(padModifier && padPressed)
		{
			s_gamepad = true;
			return true;
		}
		return false;
	}

	// --- interface ---------------------------------------------------------

	bool SliderFloat(const char * label, float * value, float lo, float hi, const char * format = "%.2f")
	{
		return ImGui::SliderFloat(label, value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
	}

	void DrawGrid()
	{
		if(s_grid == kGrid_None) return;

		ImDrawList * draw = ImGui::GetBackgroundDrawList();
		const float w = s_displayW, h = s_displayH;
		const ImU32 colour = IM_COL32(255, 255, 255, 110);
		const float thickness = 1.5f;

		switch(s_grid)
		{
			case kGrid_Thirds:
			case kGrid_Golden:
			{
				const float a = (s_grid == kGrid_Thirds) ? 1.0f / 3.0f : 0.381966f;
				const float b = 1.0f - a;
				draw->AddLine(ImVec2(w * a, 0), ImVec2(w * a, h), colour, thickness);
				draw->AddLine(ImVec2(w * b, 0), ImVec2(w * b, h), colour, thickness);
				draw->AddLine(ImVec2(0, h * a), ImVec2(w, h * a), colour, thickness);
				draw->AddLine(ImVec2(0, h * b), ImVec2(w, h * b), colour, thickness);
				break;
			}
			case kGrid_Diagonals:
				draw->AddLine(ImVec2(0, 0), ImVec2(w, h), colour, thickness);
				draw->AddLine(ImVec2(w, 0), ImVec2(0, h), colour, thickness);
				break;
			case kGrid_Center:
			{
				const float r = h * 0.03f;
				draw->AddLine(ImVec2(w * 0.5f - r, h * 0.5f), ImVec2(w * 0.5f + r, h * 0.5f), colour, thickness);
				draw->AddLine(ImVec2(w * 0.5f, h * 0.5f - r), ImVec2(w * 0.5f, h * 0.5f + r), colour, thickness);
				break;
			}
		}
	}

	void DrawCameraTab()
	{
		ImGui::Combo("Grid", &s_grid, kGridNames, kGrid_Count);

		if(float * fov = Engine::WorldFOV())
			SliderFloat("Field of view", fov, 5.0f, 150.0f, "%.0f");

		float degrees = s_roll * 180.0f / kPi;
		if(SliderFloat("Roll", &degrees, -90.0f, 90.0f, "%.0f deg"))
			s_roll = degrees * kPi / 180.0f;

		SliderFloat("Camera speed", Engine::FreeCameraSpeed(), 0.1f, 50.0f, "%.1f");
	}

	void DrawTimeTab()
	{
		if(UInt8 * freeze = Engine::FreezeTime())
		{
			bool frozen = *freeze != 0;
			if(ImGui::Checkbox("Freeze time", &frozen)) *freeze = frozen ? 1 : 0;
		}

		float multiplier = s_pendingTimeMult > 0.0f ? s_pendingTimeMult : Engine::GetGlobalTimeMultiplier();
		if(SliderFloat("Game speed", &multiplier, 0.01f, 2.0f, "%.2fx"))
			s_pendingTimeMult = multiplier;

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

		if(float * hour = Engine::GameHour())
		{
			const float h = *hour;
			char label[32];
			const int whole = (int)h;
			sprintf_s(label, "%02d:%02d", whole % 24, (int)((h - whole) * 60.0f) % 60);
			if(ImGui::SliderFloat("Time of day", hour, 0.0f, 23.99f, label, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput))
				s_hourChanged = true;
		}

		if(float * scale = Engine::TimeScale())
		{
			if(SliderFloat("Timescale", &s_timeScaleMult, 1.0f, 1000.0f, "%.0fx") && s_original.timeScale > 0.0f)
				*scale = s_original.timeScale * s_timeScaleMult;
		}

		ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));

		char preview[48];
		if(s_weatherIndex >= 0 && s_weatherIndex < (int)s_weathers.size())
			sprintf_s(preview, "Weather %08X", Engine::GetFormID(s_weathers[s_weatherIndex]));
		else
			sprintf_s(preview, "Current (%08X)", Engine::GetFormID(Engine::GetCurrentWeather()));

		if(ImGui::BeginCombo("Weather", preview, ImGuiComboFlags_HeightLarge))
		{
			for(int i = 0; i < (int)s_weathers.size(); i++)
			{
				char item[32];
				sprintf_s(item, "%08X##w%d", Engine::GetFormID(s_weathers[i]), i);
				if(ImGui::Selectable(item, i == s_weatherIndex))
				{
					s_weatherIndex = i;
					s_pendingWeather = s_weathers[i];
				}
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled("Weathers have no names in LE; IDs are shown.");
	}

	struct Hint
	{
		UInt32			key;
		const char *	label;
	};

	void DrawHints()
	{
		const Hint keyboard[] =
		{
			{ Config::TakePhotoKey, "Take photo" },
			{ Config::ToggleUIKey, "Hide UI" },
			{ Config::FreezeTimeKey, "Freeze time" },
			{ Config::ResetKey, "Reset" },
			{ Icons::kMouseRight, "Look" },
			{ kKeyEscape, "Exit" },
		};
		const Hint gamepad[] =
		{
			{ Config::GamepadTakePhoto, "Take photo" },
			{ Config::GamepadToggleUI, "Hide UI" },
			{ Config::GamepadReset, "Reset" },
			{ Config::GamepadPreviousTab, "" },
			{ Config::GamepadNextTab, "Tabs" },
			{ Config::GamepadExit, "Exit" },
		};
		const Hint * hints = s_gamepad ? gamepad : keyboard;
		const int count = s_gamepad ? (int)(sizeof(gamepad) / sizeof(gamepad[0])) : (int)(sizeof(keyboard) / sizeof(keyboard[0]));

		const float icon = ImGui::GetFontSize() * 1.15f;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float pad = s_displayH * 0.04f;

		ImGui::SetNextWindowPos(ImVec2(s_displayW * 0.5f, s_displayH - pad), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowBgAlpha(0.6f);
		ImGui::Begin("##hints", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

		for(int i = 0; i < count; i++)
		{
			if(i > 0) ImGui::SameLine(0.0f, hints[i - 1].label[0] ? spacing * 3.0f : spacing);
			ImGui::BeginGroup();
			Icons::KeyIcon(hints[i].key, icon);
			if(hints[i].label[0])
			{
				ImGui::SameLine(0.0f, spacing);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (icon - ImGui::GetTextLineHeight()) * 0.5f);
				ImGui::TextUnformatted(hints[i].label);
			}
			ImGui::EndGroup();
		}
		ImGui::End();
	}

	void DrawWindow()
	{
		const float w = s_displayW / 3.25f;
		const float h = s_displayH / 3.0f;
		ImGui::SetNextWindowPos(ImVec2(s_displayW - w * 0.5f - s_displayW * 0.03f, s_displayH * 0.62f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
		if(s_focusWindow)
		{
			ImGui::SetNextWindowFocus();
			s_focusWindow = false;
		}

		ImGui::Begin("Studio Mode", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

		if(ImGui::BeginTabBar("##tabs"))
		{
			for(int i = 0; i < kTab_Count; i++)
			{
				ImGuiTabItemFlags flags = (s_tabRequest == i) ? ImGuiTabItemFlags_SetSelected : 0;
				const char * name = Icons::HasIconFont() ? kTabIconNames[i] : kTabNames[i];
				if(ImGui::BeginTabItem(name, NULL, flags))
				{
					s_tab = i;
					ImGui::PushItemWidth(-ImGui::GetContentRegionAvail().x * 0.45f);
					switch(i)
					{
						case kTab_Camera:		DrawCameraTab(); break;
						case kTab_Time:			DrawTimeTab(); break;
						case kTab_Character:	Character::Draw(); break;
						case kTab_Filters:		Filters::Draw(); break;
					}
					ImGui::PopItemWidth();
					ImGui::EndTabItem();
				}
			}
			s_tabRequest = -1;
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void DrawToast()
	{
		if(s_toast.empty()) return;
		if(GetTickCount() > s_toastUntil) { s_toast.clear(); return; }

		ImDrawList * draw = ImGui::GetForegroundDrawList();
		const ImVec2 size = ImGui::CalcTextSize(s_toast.c_str());
		const ImVec2 pos(s_displayW * 0.5f - size.x * 0.5f, s_displayH * 0.08f);
		const float pad = ImGui::GetFontSize() * 0.5f;
		draw->AddRectFilled(ImVec2(pos.x - pad, pos.y - pad), ImVec2(pos.x + size.x + pad, pos.y + size.y + pad), IM_COL32(0, 0, 0, 160), pad);
		draw->AddText(pos, IM_COL32(255, 255, 255, 255), s_toast.c_str());
	}

	void ApplyStyle(float scale)
	{
		ImGuiStyle & style = ImGui::GetStyle();
		ImGui::StyleColorsDark(&style);
		style.WindowRounding = 6.0f;
		style.FrameRounding = 4.0f;
		style.GrabRounding = 4.0f;
		style.TabRounding = 4.0f;
		style.WindowBorderSize = 0.0f;
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.06f, 0.82f);
		style.ScaleAllSizes(scale);
	}

	bool InitImGui(IDirect3DDevice9 * device, float height)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO & io = ImGui::GetIO();
		io.IniFilename = NULL;
		io.LogFilename = NULL;
		io.BackendPlatformName = "StudioMode";
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableGamepad;
		io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

		const float scale = (height / 1080.0f) * Config::UIScale;
		Icons::SetDevice(device);
		Icons::LoadFonts(scale);
		ApplyStyle(scale < 1.0f ? 1.0f : scale);

		if(!ImGui_ImplDX9_Init(device))
		{
			_ERROR("overlay: ImGui_ImplDX9_Init failed");
			ImGui::DestroyContext();
			return false;
		}

		QueryPerformanceFrequency(&s_frequency);
		QueryPerformanceCounter(&s_lastFrame);
		_MESSAGE("overlay: ImGui %s ready, render thread %u", IMGUI_VERSION, GetCurrentThreadId());
		return true;
	}

	void RenderOverlay(IDirect3DDevice9 * device)
	{
		IDirect3DSurface9 * backBuffer = NULL;
		IDirect3DSurface9 * oldTarget = NULL;
		IDirect3DSurface9 * oldDepth = NULL;

		if(FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) return;
		device->GetRenderTarget(0, &oldTarget);
		device->GetDepthStencilSurface(&oldDepth);

		device->SetRenderTarget(0, backBuffer);
		device->SetDepthStencilSurface(NULL);

		if(SUCCEEDED(device->BeginScene()))
		{
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			device->EndScene();
		}

		device->SetRenderTarget(0, oldTarget ? oldTarget : backBuffer);
		device->SetDepthStencilSurface(oldDepth);

		if(oldDepth) oldDepth->Release();
		if(oldTarget) oldTarget->Release();
		backBuffer->Release();
	}
}

// --- public ------------------------------------------------------------------

void PhotoMode::OnDataLoaded()
{
	if(!s_lockReady)
	{
		InitializeCriticalSection(&s_lock);
		s_lockReady = true;
	}
	Screenshot::SetFolder(Config::PhotoFolder);
	s_ready = true;
}

bool PhotoMode::IsActive()
{
	return s_active;
}

bool PhotoMode::OnInput(Input::Event * head)
{
	if(!s_ready) return false;
	Lock lock;

	s_inputThread = GetCurrentThreadId();
	s_filterThisFrame = false;

	if(!s_active)
	{
		if(!ToggleKeyPressed(head) || !CanActivate()) return false;
		Activate();
		if(!s_active) return false;
		ReadControlNames();
		s_filterThisFrame = true;
		return true;		// the toggle key itself goes nowhere
	}

	// something else took the camera or opened a menu: stand down quietly
	if(Engine::IsInMenuMode() || !Engine::IsFreeCameraActive())
	{
		_MESSAGE("photo mode: %s, closing", Engine::IsInMenuMode() ? "a menu opened" : "the free camera ended");
		Deactivate(false);
		return false;
	}

	ReadControlNames();
	s_filterThisFrame = true;

	if(s_pendingTimeMult > 0.0f)
	{
		Engine::SetGlobalTimeMultiplier(s_pendingTimeMult);
		s_pendingTimeMult = -1.0f;
	}
	if(s_pendingWeather)
	{
		Engine::ForceWeather(s_pendingWeather, true);
		s_weatherForced = true;
		s_pendingWeather = NULL;
	}

	if(s_requestExit)
	{
		s_requestExit = false;
		Deactivate(true);
		return true;
	}

	Character::Update();
	Filters::Update();

	for(Input::Event * e = head; e; e = e->next)
	{
		if(e->device == Input::kDevice_Keyboard && e->type == Input::kEvent_Button)
			HandleKey((const Input::ButtonEvent *)e);
		else if(e->device == Input::kDevice_Mouse)
			HandleMouse(e);
		else if(e->device == Input::kDevice_Gamepad && e->type == Input::kEvent_Button)
			HandleGamepad((const Input::ButtonEvent *)e);
	}

	if(s_requestExit)
	{
		s_requestExit = false;
		Deactivate(true);
	}
	return true;		// filter this frame even if we just closed: Esc must not reach the game
}

bool PhotoMode::ShouldPass(const Input::Event * e)
{
	if(!s_filterThisFrame) return true;

	switch(e->type)
	{
		case Input::kEvent_Button:
			if(e->device == Input::kDevice_Keyboard)
			{
				const Input::ButtonEvent * b = (const Input::ButtonEvent *)e;
				return !s_wantText && !IsOurKey(b->keyMask) && IsPassControl(e->controlID);
			}
			return false;		// mouse and gamepad buttons belong to the interface

		case Input::kEvent_MouseMove:
			return s_active && IsPanning();

		case Input::kEvent_Thumbstick:
			return s_active;	// sticks still move and turn the camera

		case Input::kEvent_Char:
			return false;

		default:
			return true;		// device connect and the like
	}
}

float PhotoMode::GetViewRoll(float fallback)
{
	return s_active ? s_roll : fallback;
}

void PhotoMode::OnFault()
{
	if(!s_lockReady) return;
	Lock lock;
	s_filterThisFrame = false;
	if(!s_active) return;

	// the camera is only toggled where the tfc command would run
	if(GetCurrentThreadId() == s_inputThread)
	{
		_ERROR("photo mode: closing after a fault");
		Deactivate(true);
	}
	else
	{
		_ERROR("photo mode: fault on thread %u, closing on the next input frame", GetCurrentThreadId());
		s_requestExit = true;
	}
}

void PhotoMode::OnLoadGame()
{
	if(!s_lockReady) return;
	Lock lock;
	if(s_active) Deactivate(true);
	s_weathers.clear();
}

void PhotoMode::OnDeviceLost()
{
	if(!s_lockReady) return;
	Lock lock;
	if(s_imguiReady) ImGui_ImplDX9_InvalidateDeviceObjects();
	Filters::OnDeviceLost();
}

void PhotoMode::OnDeviceReset(IDirect3DDevice9 * /*device*/)
{
	if(!s_lockReady) return;
	Lock lock;
	if(s_imguiReady) ImGui_ImplDX9_CreateDeviceObjects();
	s_cursorX = s_cursorY = -1.0f;
}

void PhotoMode::OnPresent(IDirect3DDevice9 * device)
{
	if(!s_ready || !s_lockReady) return;
	Lock lock;

	// colour filters first, so the photo has them; the interface comes after the photo
	if(s_active)
		Filters::Apply(device);

	// the photo is taken before anything of ours is drawn
	if(s_photoCountdown > 0)
		s_photoCountdown--;
	else if(s_photoCountdown == 0)
	{
		s_photoCountdown = -1;
		if(Screenshot::Capture(device))
			Toast("Saving photo...");
		if(s_photoHidHUD)
		{
			Engine::ShowMenus(!s_uiHidden && s_original.menusShown);
			s_photoHidHUD = false;
		}
	}

	bool ok;
	std::string message;
	if(Screenshot::PollResult(ok, message))
		Toast(ok ? "Photo saved: " + message : message);

	const bool drawWindow = s_active && !s_uiHidden;
	const bool drawToast = !s_toast.empty();
	if(!drawWindow && !drawToast) return;
	if(s_imguiFailed) return;

	IDirect3DSurface9 * backBuffer = NULL;
	D3DSURFACE_DESC desc;
	if(FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer))) return;
	const HRESULT hr = backBuffer->GetDesc(&desc);
	backBuffer->Release();
	if(FAILED(hr)) return;

	s_displayW = (float)desc.Width;
	s_displayH = (float)desc.Height;

	if(!s_imguiReady)
	{
		s_imguiReady = InitImGui(device, s_displayH);
		if(!s_imguiReady) { s_imguiFailed = true; return; }
	}

	if(s_cursorX < 0.0f)
	{
		s_cursorX = s_displayW * 0.5f;
		s_cursorY = s_displayH * 0.5f;
		ImGui::GetIO().AddMousePosEvent(s_cursorX, s_cursorY);
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float dt = (float)(now.QuadPart - s_lastFrame.QuadPart) / (float)s_frequency.QuadPart;
	s_lastFrame = now;
	if(dt <= 0.0f || dt > 1.0f) dt = 1.0f / 60.0f;

	ImGuiIO & io = ImGui::GetIO();
	io.DisplaySize = ImVec2(s_displayW, s_displayH);
	io.DeltaTime = dt;
	io.MouseDrawCursor = drawWindow && !IsPanning() && !s_gamepad;

	ImGui_ImplDX9_NewFrame();
	ImGui::NewFrame();

	if(drawWindow)
	{
		DrawGrid();
		DrawWindow();
		DrawHints();
	}
	DrawToast();

	ImGui::Render();
	s_wantText = io.WantTextInput;
	RenderOverlay(device);
}
