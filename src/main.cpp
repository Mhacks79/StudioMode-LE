// Studio Mode
//
// A Skyrim Legendary Edition port of Photo Mode by powerofthree,
// https://github.com/powerof3/PhotoMode - GPL-3.0, as is this port.
//
// Free camera, roll, field of view, time, weather and composition grids in an
// ImGui overlay drawn through the game's Direct3D 9 device; photos are saved
// as PNG without the overlay.

#include "skse/PluginAPI.h"
#include "skse/skse_version.h"

#include "Config.h"
#include "Forms.h"
#include "Hooks.h"
#include "PhotoMode.h"
#include "Verify.h"
#include "Log.h"

#include <shlobj.h>
#include <windows.h>

#define PLUGIN_VERSION_STRING	"1.0.0"
#define PLUGIN_LOG_FILE			"\\My Games\\Skyrim\\SKSE\\StudioMode.log"

IDebugLog				gLog;
PluginHandle			g_pluginHandle	= kPluginHandle_Invalid;
SKSEMessagingInterface	* g_messaging	= NULL;

namespace
{
	bool g_installed = false;

	void OnDataLoaded()
	{
		if(g_installed) return;
		g_installed = true;

		_MESSAGE("data loaded on thread %u", GetCurrentThreadId());

		if(!Verify::Run())
		{
			_ERROR("the game does not look like the executable this plugin was built against: nothing is installed");
			return;
		}

		Forms::Finish();
		PhotoMode::OnDataLoaded();
		Hooks::InstallCamera();
		Hooks::InstallInput();
		Hooks::InstallRenderer();	// retried from the input hook if the device is not there yet

		_MESSAGE("ready: press key %02X in game to open Studio Mode", Config::ToggleKey);
	}

	void OnMessage(SKSEMessagingInterface::Message * msg)
	{
		if(!msg) return;

		switch(msg->type)
		{
			case SKSEMessagingInterface::kMessage_DataLoaded:
				OnDataLoaded();
				break;

			case SKSEMessagingInterface::kMessage_PreLoadGame:
			case SKSEMessagingInterface::kMessage_NewGame:
				if(g_installed) PhotoMode::OnLoadGame();
				break;
		}
	}
}

extern "C"
{

bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
{
#ifdef STUDIOMODE_LOG
	gLog.OpenRelative(CSIDL_MYDOCUMENTS, PLUGIN_LOG_FILE);
#endif

	_MESSAGE("Studio Mode " PLUGIN_VERSION_STRING " (built " __DATE__ " " __TIME__ ")");
	_MESSAGE("runtime %08X, editor %d", skse->runtimeVersion, skse->isEditor);

	info->infoVersion	= PluginInfo::kInfoVersion;
	info->name			= "Studio Mode";
	info->version		= 1;

	g_pluginHandle = skse->GetPluginHandle();

	if(skse->isEditor)
	{
		_MESSAGE("loaded in editor, marking as incompatible");
		return false;
	}

	if(skse->runtimeVersion != RUNTIME_VERSION_1_9_32_0)
	{
		_MESSAGE("unsupported runtime version %08X (expected %08X)", skse->runtimeVersion, RUNTIME_VERSION_1_9_32_0);
		return false;
	}

	return true;
}

bool SKSEPlugin_Load(const SKSEInterface * skse)
{
	_MESSAGE("load, thread %u", GetCurrentThreadId());

	Config::Read();

	// idles and image space modifiers are only named while the data files load
	Forms::InstallEarly();

	g_messaging = (SKSEMessagingInterface *)skse->QueryInterface(kInterface_Messaging);
	if(!g_messaging)
	{
		_MESSAGE("ABORTING: the messaging interface is not available");
		return false;
	}

	if(!g_messaging->RegisterListener(g_pluginHandle, "SKSE", OnMessage))
	{
		_MESSAGE("ABORTING: could not register for the SKSE messages");
		return false;
	}

	_MESSAGE("waiting for the game to finish loading its data files");
	return true;
}

};
