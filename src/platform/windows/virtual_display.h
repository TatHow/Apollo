#pragma once

#include <functional>
#include <vector>

#ifndef FILE_DEVICE_UNKNOWN
#define FILE_DEVICE_UNKNOWN 0x00000022
#endif

#include <ddk/d4iface.h>
#include <ddk/d4drvif.h>
#include <sudovda/sudovda.h>

namespace VDISPLAY {
	enum class DRIVER_STATUS {
		UNKNOWN              = 1,
		OK                   = 0,
		FAILED               = -1,
		VERSION_INCOMPATIBLE = -2,
		WATCHDOG_FAILED      = -3
	};

	extern HANDLE SUDOVDA_DRIVER_HANDLE;

	LONG getDeviceSettings(const wchar_t* deviceName, DEVMODEW& devMode);
	LONG changeDisplaySettings(const wchar_t* deviceName, int width, int height, int refresh_rate);
	LONG changeDisplaySettings2(const wchar_t* deviceName, int width, int height, int refresh_rate, bool bApplyIsolated=false);	
	std::wstring getPrimaryDisplay();
	bool setPrimaryDisplay(const wchar_t* primaryDeviceName);
	bool getDisplayHDRByName(const wchar_t* displayName);
	bool setDisplayHDRByName(const wchar_t* displayName, bool enableAdvancedColor);
	// Windows display scaling (percent, e.g. 250) for the display with this GDI name.
	// Returns the scaling now in effect, or -1 if it could not be read.
	int setDisplayScaleByName(const wchar_t* displayName, int percent);
	// The Windows display scaling (percent) now in effect, or -1
	int getDisplayScaleByName(const wchar_t* displayName);

	void closeVDisplayDevice();
	DRIVER_STATUS openVDisplayDevice();
	bool startPingThread(std::function<void()> failCb);
	bool setRenderAdapterByName(const std::wstring& adapterName);
	std::wstring createVirtualDisplay(
		const char* s_client_uid,
		const char* s_client_name,
		uint32_t width,
		uint32_t height,
		uint32_t fps,
		const GUID& guid
	);
	bool removeVirtualDisplay(const GUID& guid);

	std::vector<std::wstring> matchDisplay(std::wstring sMatch);
}
