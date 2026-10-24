// Config.cpp
//
// GetPrivateProfileString, then our own decimal parser: the C runtime's
// strtod follows the user's locale, and "0.5" must not read as 0 on a
// French system (rules/FATAL.md).

#include "Config.h"
#include "Log.h"

#include <windows.h>

namespace Config
{
	UInt32		ToggleKey			= 0xC7;	// Home
	UInt32		TakePhotoKey		= 0xB7;	// Print Screen
	UInt32		ToggleUIKey			= 0x14;	// T
	UInt32		FreezeTimeKey		= 0x21;	// F
	UInt32		ResetKey			= 0x13;	// R
	UInt32		PanCameraKey		= 0x2A;	// Left Shift
	UInt32		NextTabKey			= 0x12;	// E
	UInt32		PreviousTabKey		= 0x10;	// Q

	UInt32		GamepadToggle			= 275;	// RB...
	UInt32		GamepadToggleModifier	= 274;	// ...while LB is held
	UInt32		GamepadExit				= 270;	// Start
	UInt32		GamepadTakePhoto		= 271;	// Back
	UInt32		GamepadToggleUI			= 278;	// X
	UInt32		GamepadReset			= 279;	// Y
	UInt32		GamepadNextTab			= 281;	// RT
	UInt32		GamepadPreviousTab		= 280;	// LT

	bool		FreezeTimeOnStart	= false;
	float		FreeCameraSpeed		= -1.0f;
	float		CursorSpeed			= 1.0f;
	float		UIScale				= 1.0f;
	bool		HideHUDForPhotos	= true;
	std::string	PhotoFolder;

	std::string	FontFile			= "Jost-Regular.ttf";
	float		FontSize			= 27.0f;
	float		IconFontSize		= 20.0f;
}

namespace
{
	char s_path[MAX_PATH];

	std::string ReadString(const char * section, const char * key, const char * fallback)
	{
		char buf[MAX_PATH];
		GetPrivateProfileStringA(section, key, fallback, buf, sizeof(buf), s_path);
		return buf;
	}

	UInt32 ReadUInt(const char * section, const char * key, UInt32 fallback)
	{
		return (UInt32)GetPrivateProfileIntA(section, key, (int)fallback, s_path);
	}

	bool ReadBool(const char * section, const char * key, bool fallback)
	{
		return GetPrivateProfileIntA(section, key, fallback ? 1 : 0, s_path) != 0;
	}

	// [-]digits[.digits], nothing else; anything malformed keeps the fallback
	float ReadFloat(const char * section, const char * key, float fallback)
	{
		const std::string text = ReadString(section, key, "");
		const char * p = text.c_str();
		while(*p == ' ' || *p == '\t') p++;

		bool negative = false;
		if(*p == '-') { negative = true; p++; }
		else if(*p == '+') p++;

		double value = 0.0, scale = 1.0;
		bool digits = false, fraction = false;
		for(; *p; p++)
		{
			if(*p >= '0' && *p <= '9')
			{
				digits = true;
				if(fraction) { scale /= 10.0; value += (*p - '0') * scale; }
				else value = value * 10.0 + (*p - '0');
			}
			else if(*p == '.' && !fraction) fraction = true;
			else if(*p == ' ' || *p == '\t' || *p == ';') break;
			else return fallback;
		}

		if(!digits) return fallback;
		return (float)(negative ? -value : value);
	}
}

void Config::Read()
{
	GetModuleFileNameA(NULL, s_path, sizeof(s_path));
	char * slash = strrchr(s_path, '\\');
	if(slash) *(slash + 1) = 0;
	strcat_s(s_path, "Data\\SKSE\\Plugins\\StudioMode.ini");

	ToggleKey			= ReadUInt("Controls", "iToggleKey", ToggleKey);
	TakePhotoKey		= ReadUInt("Controls", "iTakePhotoKey", TakePhotoKey);
	ToggleUIKey			= ReadUInt("Controls", "iToggleMenusKey", ToggleUIKey);
	FreezeTimeKey		= ReadUInt("Controls", "iFreezeTimeKey", FreezeTimeKey);
	ResetKey			= ReadUInt("Controls", "iResetKey", ResetKey);
	PanCameraKey		= ReadUInt("Controls", "iPanCameraKey", PanCameraKey);
	NextTabKey			= ReadUInt("Controls", "iNextTabKey", NextTabKey);
	PreviousTabKey		= ReadUInt("Controls", "iPreviousTabKey", PreviousTabKey);

	GamepadToggle			= ReadUInt("Controls", "iToggleGamePad", GamepadToggle);
	GamepadToggleModifier	= ReadUInt("Controls", "iToggleGamePadModifier", GamepadToggleModifier);
	GamepadExit				= ReadUInt("Controls", "iExitGamePad", GamepadExit);
	GamepadTakePhoto		= ReadUInt("Controls", "iTakePhotoGamePad", GamepadTakePhoto);
	GamepadToggleUI			= ReadUInt("Controls", "iToggleMenusGamePad", GamepadToggleUI);
	GamepadReset			= ReadUInt("Controls", "iResetGamePad", GamepadReset);
	GamepadNextTab			= ReadUInt("Controls", "iNextTabGamePad", GamepadNextTab);
	GamepadPreviousTab		= ReadUInt("Controls", "iPreviousTabGamePad", GamepadPreviousTab);

	FreezeTimeOnStart	= ReadBool("Settings", "bFreezeTimeOnStart", FreezeTimeOnStart);
	FreeCameraSpeed		= ReadFloat("Settings", "fFreeCameraTranslationSpeed", FreeCameraSpeed);
	CursorSpeed			= ReadFloat("Settings", "fCursorSpeed", CursorSpeed);
	UIScale				= ReadFloat("Settings", "fUIScale", UIScale);

	HideHUDForPhotos	= ReadBool("Screenshots", "bAutoHideMenus", HideHUDForPhotos);
	PhotoFolder			= ReadString("Screenshots", "sPhotoFolder", "");

	FontFile			= ReadString("Fonts", "sFont", FontFile.c_str());
	FontSize			= ReadFloat("Fonts", "iFontSize", FontSize);
	IconFontSize		= ReadFloat("Fonts", "iIconSize", IconFontSize);
	if(FontSize < 8.0f || FontSize > 96.0f) FontSize = 27.0f;
	if(IconFontSize < 8.0f || IconFontSize > 96.0f) IconFontSize = 20.0f;
	if(FontFile.find_first_of("\\/:") != std::string::npos) FontFile = "Jost-Regular.ttf";	// a file name, not a path

	if(CursorSpeed < 0.1f || CursorSpeed > 10.0f) CursorSpeed = 1.0f;
	if(UIScale < 0.5f || UIScale > 3.0f) UIScale = 1.0f;

	_MESSAGE("config: %s", s_path);
	_MESSAGE("config: gamepad toggle %u (+%u), exit %u, photo %u, hide %u, reset %u, tabs %u/%u",
		GamepadToggle, GamepadToggleModifier, GamepadExit, GamepadTakePhoto, GamepadToggleUI, GamepadReset, GamepadPreviousTab, GamepadNextTab);
	_MESSAGE("config: toggle %02X, photo %02X, hide %02X, freeze %02X, reset %02X, pan %02X, tabs %02X/%02X",
		ToggleKey, TakePhotoKey, ToggleUIKey, FreezeTimeKey, ResetKey, PanCameraKey, PreviousTabKey, NextTabKey);
}
