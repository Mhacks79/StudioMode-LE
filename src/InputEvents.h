// InputEvents.h - the engine's input events, as laid out in TESV.exe 1.9.32.0.8.
//
// SKSE declares ButtonEvent as IDEvent + InputEvent; the polymorphic base comes
// first in memory. The event pool initialiser at 0x00A69230 writes the
// ButtonEvent vtable 0x010D44E0 at +0, device at +4, type at +8, next at +0C,
// and -1 at +14. LookHandler::ProcessMouseMove 0x00770FB0 reads the mouse
// deltas with fild [event+18] / fild [event+1C]: they are int32.

#pragma once

namespace Input
{
	enum DeviceType
	{
		kDevice_Keyboard = 0,
		kDevice_Mouse,
		kDevice_Gamepad,
	};

	enum EventType
	{
		kEvent_Button = 0,
		kEvent_MouseMove,
		kEvent_Char,
		kEvent_Thumbstick,
		kEvent_DeviceConnect,
		kEvent_Kinect,
	};

	enum MouseButton
	{
		kMouse_Left = 0,
		kMouse_Right = 1,
		kMouse_Middle = 2,
		kMouse_WheelUp = 8,
		kMouse_WheelDown = 9,
	};

	struct Event
	{
		void *			vtbl;		// 00
		UInt32			device;		// 04
		UInt32			type;		// 08
		Event *			next;		// 0C
		const char *	controlID;	// 10 BSFixedString, compared by pointer
	};

	struct ButtonEvent : Event
	{
		UInt32			keyMask;	// 14 scan code, mouse button, gamepad mask
		float			value;		// 18 non-zero while held
		float			timer;		// 1C seconds held; 0 on the press
	};

	struct MouseMoveEvent : Event
	{
		UInt32			keyMask;	// 14
		SInt32			dx;			// 18
		SInt32			dy;			// 1C
	};

	inline bool IsDown(const ButtonEvent * e)		{ return e->value != 0.0f && e->timer == 0.0f; }
	inline bool IsUp(const ButtonEvent * e)			{ return e->value == 0.0f && e->timer != 0.0f; }
	inline bool IsHeld(const ButtonEvent * e)		{ return e->value != 0.0f; }

	// InputStringHolder offsets (SKSE GameInput.h)
	enum ControlName
	{
		kControl_Forward		= 0x04,
		kControl_Back			= 0x08,
		kControl_StrafeLeft		= 0x0C,
		kControl_StrafeRight	= 0x10,
		kControl_Move			= 0x14,
		kControl_Look			= 0x18,
	};
}
