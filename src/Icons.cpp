// Icons.cpp
//
// The PNGs are decoded with WIC (part of Windows) into managed D3D9 textures,
// which survive a device reset. They are loaded on first use, on the render
// thread, and kept for the life of the process.

#include "Icons.h"
#include "Config.h"
#include "Log.h"

#include "imgui.h"

#include <windows.h>
#include <d3d9.h>
#include <wincodec.h>

#include <map>
#include <string>

namespace
{
	struct Texture
	{
		IDirect3DTexture9 *	texture;
		float				width;
		float				height;
	};

	struct KeyEntry
	{
		UInt32			key;
		const char *	file;	// Data\Interface\ImGuiIcons\Icons\<file>.png
		const char *	name;	// text fallback
	};

	// DirectInput scan codes, plus 0x100 + mouse button; file names as in the SE mod
	const KeyEntry kKeys[] =
	{
		{ 0x01, "Esc", "Esc" }, { 0x02, "1", "1" }, { 0x03, "2", "2" }, { 0x04, "3", "3" }, { 0x05, "4", "4" },
		{ 0x06, "5", "5" }, { 0x07, "6", "6" }, { 0x08, "7", "7" }, { 0x09, "8", "8" }, { 0x0A, "9", "9" },
		{ 0x0B, "0", "0" }, { 0x0C, "Hyphen", "-" }, { 0x0D, "Equal", "=" }, { 0x0E, "Backspace", "Backspace" },
		{ 0x0F, "Tab", "Tab" }, { 0x10, "Q", "Q" }, { 0x11, "W", "W" }, { 0x12, "E", "E" }, { 0x13, "R", "R" },
		{ 0x14, "T", "T" }, { 0x15, "Y", "Y" }, { 0x16, "U", "U" }, { 0x17, "I", "I" }, { 0x18, "O", "O" },
		{ 0x19, "P", "P" }, { 0x1A, "Bracketleft", "[" }, { 0x1B, "Bracketright", "]" }, { 0x1C, "Enter", "Enter" },
		{ 0x1D, "L-Ctrl", "LCtrl" }, { 0x1E, "A", "A" }, { 0x1F, "S", "S" }, { 0x20, "D", "D" }, { 0x21, "F", "F" },
		{ 0x22, "G", "G" }, { 0x23, "H", "H" }, { 0x24, "J", "J" }, { 0x25, "K", "K" }, { 0x26, "L", "L" },
		{ 0x27, "Semicolon", ";" }, { 0x28, "Quotesingle", "'" }, { 0x29, "Tilde", "~" }, { 0x2A, "L-Shift", "LShift" },
		{ 0x2B, "Backslash", "\\" }, { 0x2C, "Z", "Z" }, { 0x2D, "X", "X" }, { 0x2E, "C", "C" }, { 0x2F, "V", "V" },
		{ 0x30, "B", "B" }, { 0x31, "N", "N" }, { 0x32, "M", "M" }, { 0x33, "Comma", "," }, { 0x34, "Period", "." },
		{ 0x35, "Slash", "/" }, { 0x36, "R-Shift", "RShift" }, { 0x37, "NumPadMult", "Num*" }, { 0x38, "L-Alt", "LAlt" },
		{ 0x39, "Space", "Space" }, { 0x3A, "CapsLock", "Caps" }, { 0x3B, "F1", "F1" }, { 0x3C, "F2", "F2" },
		{ 0x3D, "F3", "F3" }, { 0x3E, "F4", "F4" }, { 0x3F, "F5", "F5" }, { 0x40, "F6", "F6" }, { 0x41, "F7", "F7" },
		{ 0x42, "F8", "F8" }, { 0x43, "F9", "F9" }, { 0x44, "F10", "F10" }, { 0x45, "NumLock", "NumLock" },
		{ 0x46, "ScrollLock", "ScrollLock" }, { 0x47, "Keypad7", "Num7" }, { 0x48, "Keypad8", "Num8" },
		{ 0x49, "NumPad9", "Num9" }, { 0x4A, "NumPadMinus", "Num-" }, { 0x4B, "Keypad4", "Num4" }, { 0x4C, "Keypad5", "Num5" },
		{ 0x4D, "Keypad6", "Num6" }, { 0x4E, "NumPadPlus", "Num+" }, { 0x4F, "Keypad1", "Num1" }, { 0x50, "Keypad2", "Num2" },
		{ 0x51, "Keypad3", "Num3" }, { 0x52, "NumPad0", "Num0" }, { 0x53, "NumPadDec", "Num." }, { 0x57, "F11", "F11" },
		{ 0x58, "F12", "F12" }, { 0x9C, "KeypadEnter", "NumEnter" }, { 0x9D, "R-Ctrl", "RCtrl" }, { 0xB5, "NumPadDivide", "Num/" },
		{ 0xB7, "PrintScreen", "PrtSc" }, { 0xB8, "R-Alt", "RAlt" }, { 0xC5, "Pause", "Pause" }, { 0xC7, "Home", "Home" },
		{ 0xC8, "Up", "Up" }, { 0xC9, "PgUp", "PgUp" }, { 0xCB, "Left", "Left" }, { 0xCD, "Right", "Right" },
		{ 0xCF, "End", "End" }, { 0xD0, "Down", "Down" }, { 0xD1, "PgDn", "PgDn" }, { 0xD2, "Insert", "Insert" },
		{ 0xD3, "Delete", "Delete" },
		{ Icons::kMouseLeft, "Mouse1", "Left mouse" }, { Icons::kMouseRight, "Mouse2", "Right mouse" },
		{ Icons::kMouseMiddle, "Mouse3", "Middle mouse" }, { Icons::kMouseMove, "MouseMove", "Mouse" },
		// gamepad, SKSE key codes; Xbox icons as the SE mod's default scheme
		{ 266, "Up", "D-pad up" }, { 267, "Down", "D-pad down" }, { 268, "Left", "D-pad left" }, { 269, "Right", "D-pad right" },
		{ 270, "360_Start", "Start" }, { 271, "360_Back", "Back" }, { 272, "360_LS", "LS" }, { 273, "360_RS", "RS" },
		{ 274, "360_LB", "LB" }, { 275, "360_RB", "RB" }, { 276, "360_A", "A" }, { 277, "360_B", "B" },
		{ 278, "360_X", "X" }, { 279, "360_Y", "Y" }, { 280, "360_LT", "LT" }, { 281, "360_RT", "RT" },
	};

	std::string						s_root;		// ...\Data\Interface\ImGuiIcons\ (trailing slash)
	bool							s_iconFont = false;
	IDirect3DDevice9 *				s_device = NULL;
	std::map<std::string, Texture>	s_textures;	// a null texture marks a file that failed to load

	const KeyEntry * FindKey(UInt32 key)
	{
		for(UInt32 i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++)
			if(kKeys[i].key == key) return &kKeys[i];
		return NULL;
	}

	const std::string & Root()
	{
		if(s_root.empty())
		{
			char path[MAX_PATH];
			GetModuleFileNameA(NULL, path, MAX_PATH);
			char * slash = strrchr(path, '\\');
			if(slash) *(slash + 1) = 0;
			s_root = std::string(path) + "Data\\Interface\\ImGuiIcons\\";
		}
		return s_root;
	}

	bool Exists(const std::string & path)
	{
		return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	IDirect3DTexture9 * Decode(const std::string & path, UINT & width, UINT & height)
	{
		IWICImagingFactory *	factory = NULL;
		IWICBitmapDecoder *		decoder = NULL;
		IWICBitmapFrameDecode *	frame = NULL;
		IWICFormatConverter *	converter = NULL;
		IDirect3DTexture9 *		texture = NULL;

		wchar_t wpath[MAX_PATH];
		MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wpath, MAX_PATH);

		HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if(SUCCEEDED(hr)) hr = factory->CreateDecoderFromFilename(wpath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
		if(SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
		if(SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
		if(SUCCEEDED(hr)) hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
		if(SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
		if(SUCCEEDED(hr) && (width == 0 || height == 0 || width > 4096 || height > 4096)) hr = E_FAIL;
		if(SUCCEEDED(hr)) hr = s_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL);

		if(SUCCEEDED(hr))
		{
			D3DLOCKED_RECT locked;
			hr = texture->LockRect(0, &locked, NULL, 0);
			if(SUCCEEDED(hr))
			{
				// BGRA in memory is D3DFMT_A8R8G8B8
				hr = converter->CopyPixels(NULL, (UINT)locked.Pitch, (UINT)locked.Pitch * height, (BYTE *)locked.pBits);
				texture->UnlockRect(0);
			}
		}

		if(FAILED(hr) && texture) { texture->Release(); texture = NULL; }
		if(converter) converter->Release();
		if(frame) frame->Release();
		if(decoder) decoder->Release();
		if(factory) factory->Release();

		if(!texture) _WARNING("icons: could not load %s (HRESULT %08X)", path.c_str(), hr);
		return texture;
	}

	const Texture * Get(const char * file)
	{
		std::map<std::string, Texture>::iterator it = s_textures.find(file);
		if(it != s_textures.end())
			return it->second.texture ? &it->second : NULL;

		Texture t = { NULL, 0.0f, 0.0f };
		const std::string path = Root() + "Icons\\" + file + ".png";
		if(s_device && Exists(path))
		{
			// the render thread may not have COM yet; any apartment will do for WIC
			const HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
			UINT w = 0, h = 0;
			t.texture = Decode(path, w, h);
			t.width = (float)w;
			t.height = (float)h;
			if(SUCCEEDED(co)) CoUninitialize();
		}

		s_textures[file] = t;
		return t.texture ? &s_textures[file] : NULL;
	}
}

void Icons::LoadFonts(float scale)
{
	ImGuiIO & io = ImGui::GetIO();

	const float fontSize = floorf(Config::FontSize * scale) < 12.0f ? 12.0f : floorf(Config::FontSize * scale);
	const float iconSize = floorf(Config::IconFontSize * scale) < 10.0f ? 10.0f : floorf(Config::IconFontSize * scale);

	ImFont * font = NULL;
	const std::string text = Root() + "Fonts\\" + Config::FontFile;
	if(Exists(text))
		font = io.Fonts->AddFontFromFileTTF(text.c_str(), fontSize);

	if(!font)
	{
		char system[MAX_PATH];
		GetWindowsDirectoryA(system, MAX_PATH);
		strcat_s(system, "\\Fonts\\segoeui.ttf");
		if(Exists(system))
			font = io.Fonts->AddFontFromFileTTF(system, fontSize * 0.75f);
	}

	if(!font)
	{
		ImFontConfig config;
		config.SizePixels = fontSize * 0.75f;
		font = io.Fonts->AddFontDefaultVector(&config);
	}

	const std::string awesome = Root() + "Fonts\\fa-solid-900.ttf";
	if(Exists(awesome))
	{
		ImFontConfig config;
		config.MergeMode = true;
		config.PixelSnapH = true;
		config.GlyphMinAdvanceX = iconSize;
		s_iconFont = io.Fonts->AddFontFromFileTTF(awesome.c_str(), iconSize, &config) != NULL;
	}

	ImGui::GetStyle().FontSizeBase = font ? font->LegacySize : fontSize;

	_MESSAGE("icons: ImGui Icons %s; font %s, %.0f px; Font Awesome %s",
		Exists(Root()) ? "found" : "not installed (text fallback)",
		Exists(text) ? Config::FontFile.c_str() : "fallback", ImGui::GetStyle().FontSizeBase,
		s_iconFont ? "on" : "off");
}

bool Icons::HasIconFont()
{
	return s_iconFont;
}

void Icons::SetDevice(IDirect3DDevice9 * device)
{
	s_device = device;
}

const char * Icons::KeyName(UInt32 key)
{
	const KeyEntry * entry = FindKey(key);
	return entry ? entry->name : "?";
}

float Icons::KeyIconWidth(UInt32 key, float height)
{
	const KeyEntry * entry = FindKey(key);
	const Texture * t = entry ? Get(entry->file) : NULL;
	if(t) return t->width * (height / t->height);

	char text[48];
	sprintf_s(text, "[%s]", KeyName(key));
	return ImGui::CalcTextSize(text).x;
}

float Icons::KeyIcon(UInt32 key, float height)
{
	const KeyEntry * entry = FindKey(key);
	const Texture * t = entry ? Get(entry->file) : NULL;
	if(t)
	{
		const ImVec2 size(t->width * (height / t->height), height);
		ImGui::Image(ImTextureRef((ImTextureID)(UINT_PTR)t->texture), size);
		return size.x;
	}

	char text[48];
	sprintf_s(text, "[%s]", KeyName(key));
	ImGui::TextUnformatted(text);
	return ImGui::CalcTextSize(text).x;
}
