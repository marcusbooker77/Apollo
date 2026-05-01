#pragma once

#include <functional>
#include <memory>
#include <vector>

#ifndef FILE_DEVICE_UNKNOWN
#define FILE_DEVICE_UNKNOWN 0x00000022
#endif

#include <ddk/d4iface.h>
#include <ddk/d4drvif.h>
#include <sudovda/sudovda.h>

namespace VDISPLAY {
	/**
	 * @brief RAII snapshot of the active Windows display topology.
	 *
	 * Captures the exact path + mode arrays returned by QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)
	 * at construction time. The destructor restores that exact configuration via
	 * SetDisplayConfig with SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE so the
	 * user's pre-stream layout (extend / clone / internal-only / multi-monitor positions)
	 * is preserved across normal exits, exception unwinds, and signal-handler exits.
	 *
	 * If the initial QueryDisplayConfig call fails, the snapshot is marked invalid and the
	 * destructor becomes a no-op rather than restoring garbage.
	 */
	class display_topology_snapshot_t {
	public:
		display_topology_snapshot_t();
		~display_topology_snapshot_t();

		display_topology_snapshot_t(const display_topology_snapshot_t&) = delete;
		display_topology_snapshot_t& operator=(const display_topology_snapshot_t&) = delete;
		display_topology_snapshot_t(display_topology_snapshot_t&&) = delete;
		display_topology_snapshot_t& operator=(display_topology_snapshot_t&&) = delete;

		bool valid() const { return _valid; }

	private:
		std::vector<DISPLAYCONFIG_PATH_INFO> _paths;
		std::vector<DISPLAYCONFIG_MODE_INFO> _modes;
		bool _valid = false;
	};

	/**
	 * @brief Accessor for the process-wide topology snapshot slot.
	 *
	 * Held in a function-local static std::unique_ptr so it survives every exit path that
	 * runs static destructors (normal return from main, std::exit, std::terminate after
	 * uncaught exception, signal handler that calls std::raise + return). The proc layer
	 * resets it explicitly in normal teardown; the static destructor is the safety net for
	 * abnormal exits.
	 */
	std::unique_ptr<display_topology_snapshot_t>& topology_snapshot_slot();
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
