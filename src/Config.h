// Config.h - Data\SKSE\Plugins\StudioMode.ini
#pragma once

#include <string>

namespace Config
{
	// keys are DirectInput scan codes, as in SKSE and the MCM
	extern UInt32		ToggleKey;			// open / close Studio Mode
	extern UInt32		TakePhotoKey;
	extern UInt32		ToggleUIKey;
	extern UInt32		FreezeTimeKey;
	extern UInt32		ResetKey;
	extern UInt32		PanCameraKey;		// held: the mouse turns the camera (right mouse does too)
	extern UInt32		NextTabKey;
	extern UInt32		PreviousTabKey;

	// gamepad buttons as SKSE key codes (266 D-pad up ... 281 RT); 0 = none
	extern UInt32		GamepadToggle;			// pressed while GamepadToggleModifier is held
	extern UInt32		GamepadToggleModifier;
	extern UInt32		GamepadExit;
	extern UInt32		GamepadTakePhoto;
	extern UInt32		GamepadToggleUI;
	extern UInt32		GamepadReset;
	extern UInt32		GamepadNextTab;
	extern UInt32		GamepadPreviousTab;

	extern bool			FreezeTimeOnStart;
	extern float		FreeCameraSpeed;	// < 0: keep the game's fFreeCameraTranslationSpeed
	extern float		CursorSpeed;
	extern float		UIScale;
	extern bool			HideHUDForPhotos;
	extern std::string	PhotoFolder;		// empty: My Games\Skyrim\PhotoMode

	// fonts from Data\Interface\ImGuiIcons\Fonts, sizes at 1080p (as the SE fonts.ini)
	extern std::string	FontFile;
	extern float		FontSize;
	extern float		IconFontSize;

	void Read();
}
