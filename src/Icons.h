// Icons.h - fonts and key icons from the "ImGui Icons" mod (Nexus SE 114790).
//
// It is a data-only mod (Data\Interface\ImGuiIcons\Fonts and \Icons), so the
// same files serve LE. Everything here falls back to plain text and Segoe UI
// when it is not installed.

#pragma once

struct IDirect3DDevice9;

// Font Awesome 6 solid, as used by the original Photo Mode
#define ICON_FA_CAMERA	"\xef\x80\xb0"	// U+F030
#define ICON_FA_CLOCK	"\xef\x80\x97"	// U+F017
#define ICON_FA_PERSON	"\xef\x86\x83"	// U+F183
#define ICON_FA_CIRCLE_HALF_STROKE	"\xef\x81\x82"	// U+F042

namespace Icons
{
	enum Mouse
	{
		kMouseLeft = 0x100,
		kMouseRight,
		kMouseMiddle,
		kMouseMove = 0x1FF,		// not a button: the MouseMove icon
	};

	// adds the fonts to the ImGui atlas; call before the renderer backend starts
	void	LoadFonts(float resolutionScale);
	bool	HasIconFont();				// Font Awesome glyphs can be used in labels

	void	SetDevice(IDirect3DDevice9 * device);

	// draws the key's icon, or "[Name]" when the icon is missing; returns its width
	float	KeyIcon(UInt32 key, float height);
	float	KeyIconWidth(UInt32 key, float height);
	const char * KeyName(UInt32 key);
}
