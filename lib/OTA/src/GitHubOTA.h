#ifndef ESP_GITHUB_OTA_H
#define ESP_GITHUB_OTA_H

#include "ControllerOTA.h"
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "semver.h"

constexpr uint8_t PHASE_IDLE = 0;
constexpr uint8_t PHASE_DISPLAY_FW = 1;
constexpr uint8_t PHASE_DISPLAY_FS = 2;
constexpr uint8_t PHASE_CONTROLLER_FW = 3;
constexpr uint8_t PHASE_FINISHED = 4;

using phase_callback_t = std::function<void(uint8_t phase)>;
using progress_callback_t = std::function<void(uint8_t phase, int progress)>;

extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");

class GitHubOTA {
  public:
    GitHubOTA(const String &display_version, const String &controller_version, const String &release_url,
              const phase_callback_t &phase_callback, const progress_callback_t &progress_callback,
              const String &firmware_name = "firmware.bin", const String &filesystem_name = "filesystem.bin",
              const String &controller_firmware_name = "controller.bin");

    void init(NimBLEClient *client);
    ~GitHubOTA();
    void checkForUpdates();
    bool isUpdateAvailable(bool controller = false) const;
    String getCurrentVersion() const;
    // Authoritative resolve-failure signal from the last checkForUpdates().
    // getCurrentVersion() cannot be trusted for this: on a failed resolve it
    // returns the STALE _latest_version_string from a prior successful check
    // (never cleared on the failure early-return), so emptiness alone can
    // mask a failure. Callers gating on "did the resolve fail" must use this.
    bool isUpdateCheckFailed() const { return _update_check_failed; }
    // PRO-403: whether the controller component was actually flashed during the
    // last update() call. On the default two-component flash the controller can
    // flash successfully and the display then fail, so update() returns false
    // while the controller is already running the new image. A caller restoring
    // a persisted channel marker on failure must NOT restore in that case, or
    // the marker will disagree with the on-device controller.
    bool didFlashControllerLastUpdate() const { return _last_update_flashed_controller; }
    bool update(bool controller = true, bool display = true, bool force = false);
    void setReleaseUrl(const String &release_url);
    void setControllerVersion(const String &controller_version);

  private:
    HTTPUpdate Updater;

    HTTPUpdateResult update_firmware(const String &url);

    uint8_t phase = PHASE_IDLE;
    semver_t _version;
    semver_t _controller_version;
    String _latest_version_string;
    semver_t _latest_version = {0, 0, 0, nullptr, nullptr};
    bool _update_check_failed = false;
    bool _last_update_flashed_controller = false;
    String _release_url;
    String _latest_url;
    String _firmware_name;
    String _filesystem_name;
    String _controller_firmware_name;
    WiFiClientSecure _wifi_client;
    ControllerOTA _controller_ota;
    phase_callback_t _phase_callback = nullptr;
    progress_callback_t _progress_callback = nullptr;
};

#endif
