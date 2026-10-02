// Desktop simulator entry point. Runs the real display Controller on a single
// cooperative loop on the main thread (LVGL/SDL must stay on the main thread on
// macOS), driving the firmware's loop methods directly since the FreeRTOS tasks
// are no-ops in the simulator.
#include "ESPAsyncWebServer.h"
#include "SdlDriver.h"
#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <display/core/Controller.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <display/ui/default/DefaultUI.h>

// The generated UI event handlers reference this global (see main.h on device).
Controller controller;

// PRO-674: `--link-check` drives scripted BLE link scenarios against the real
// Controller + DefaultUI and checks the ACTIVE LVGL screen matches the Controller
// mode after each. Prints PASS/FAIL per check, exits 1 on any failure.
// `--link-check-explicit-standby` runs the explicit-STANDBY-during-link-drop variant.
static void simStep(SdlDriver *drv, DefaultUI *ui, unsigned long ms) {
    const unsigned long until = millis() + ms;
    while ((long)(millis() - until) < 0) {
        controller.loop();
        controller.loopControl();
        if (ui) {
            ui->loop();
        }
        drv->pumpAndRender();
        delay(5);
    }
}

static const char *screenName(lv_obj_t *s) {
    if (s == ui_StandbyScreen)
        return "Standby";
    if (s == ui_BrewScreen)
        return "Brew";
    if (s == ui_StatusScreen)
        return "Status";
    return "other";
}

static int runLinkCheck(SdlDriver *drv, DefaultUI *ui, bool explicitStandbyVariant) {
    GaggiMateClient *comms = controller.getClientController();
    int failures = 0;
    auto check = [&](const char *what, int wantMode, lv_obj_t *wantScreen) {
        const int m = controller.getMode();
        lv_obj_t *act = lv_scr_act();
        const bool ok = m == wantMode && act == wantScreen;
        printf("[link-check] %s %s: mode=%d (want %d) screen=%s (want %s)\n", ok ? "PASS" : "FAIL", what, m, wantMode,
               screenName(act), screenName(wantScreen));
        fflush(stdout);
        failures += ok ? 0 : 1;
    };
    auto reconnect = [&](uint32_t version) {
        comms->simHoldDown(true);
        comms->simDropLink();
        simStep(drv, ui, 300);
        comms->simSetProtocolVersion(version);
        comms->simHoldDown(false);
        simStep(drv, ui, 800);
    };
    controller.getSettings().setStartupMode(MODE_BREW);
    // 1. Boot against a mismatched controller (mismatch-forced standby), then the
    // controller is fixed (link drop + matching reconnect): startup BREW is restored.
    // (A mismatch first seen on a RECONNECT never sets the flag: the drop already
    // put the Controller in STANDBY, see systemInfoModeAction's "late mismatch
    // already in standby" row.)
    comms->simSetProtocolVersion(gm_proto::PROTOCOL_VERSION - 1);
    simStep(drv, ui, 1500); // first link + mismatched SystemInfo
    check("boot against mismatched controller forces standby", MODE_STANDBY, ui_StandbyScreen);
    if (explicitStandbyVariant) {
        // PRO-674 item 1: an explicit user STANDBY while a mismatch-forced standby's
        // link is down ends the forced state; the matching reconnect must NOT restore BREW.
        comms->simHoldDown(true);
        comms->simDropLink();
        simStep(drv, ui, 300);
        controller.activateStandby();
        comms->simSetProtocolVersion(gm_proto::PROTOCOL_VERSION);
        comms->simHoldDown(false);
        simStep(drv, ui, 800);
        check("explicit STANDBY during mismatch link drop is kept on matching reconnect", MODE_STANDBY, ui_StandbyScreen);
        printf("[link-check] %s (%d failure(s))\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
        return failures == 0 ? 0 : 1;
    }
    reconnect(gm_proto::PROTOCOL_VERSION);
    check("matching reconnect after mismatch-standby restores startup BREW", MODE_BREW, ui_BrewScreen);

    // 3. User-initiated STANDBY during a live link.
    controller.activateStandby();
    simStep(drv, ui, 500);
    check("user STANDBY during live link", MODE_STANDBY, ui_StandbyScreen);

    // 2. Ordinary reconnect while in (user) STANDBY: must stay STANDBY even with startup=BREW.
    reconnect(gm_proto::PROTOCOL_VERSION);
    check("ordinary reconnect while STANDBY stays standby", MODE_STANDBY, ui_StandbyScreen);

    printf("[link-check] %s (%d failure(s))\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    // Optional: `--screenshot <path> [delayMs]` renders for a bit, saves a BMP, exits.
    const char *shotPath = nullptr;
    bool linkCheck = false;
    bool linkCheckExplicit = false;
    unsigned long shotDelayMs = 4000;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--link-check") == 0) {
            linkCheck = true;
        }
        if (strcmp(argv[i], "--link-check-explicit-standby") == 0) {
            linkCheck = true;
            linkCheckExplicit = true;
        }
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
            if (i + 1 < argc)
                shotDelayMs = strtoul(argv[i + 1], nullptr, 10);
        }
    }

    controller.setup(); // builds the UI, installs the SDL driver, marks screen ready

    // The sim has a real network (the WebUI is reachable), so present as Wi-Fi
    // connected: seeding credentials sends setupWifi() down the STA path, and the
    // WiFi shim's begin() reports WL_CONNECTED. This makes the standby screen show
    // the clock and Wi-Fi icon (and avoids captive-portal AP mode). Seed only once.
    Settings &settings = controller.getSettings();
    if (settings.getWifiSsid().isEmpty())
        settings.setWifiSsid("GaggiMate-Sim");
    if (settings.getWifiPassword().isEmpty())
        settings.setWifiPassword("simulator");

    SdlDriver *drv = SdlDriver::getInstance();
    DefaultUI *ui = controller.getUI();
    const unsigned long start = millis();
    bool shotTaken = false;
    if (linkCheck) {
        return runLinkCheck(drv, ui, linkCheckExplicit);
    }

    while (!drv->shouldQuit()) {
        controller.loop();        // connection lifecycle, comms pump, plugins
        controller.loopControl(); // process + control logic (normally a FreeRTOS task)

        // Shot history sampling normally runs in its own FreeRTOS task (a no-op in
        // the sim), so drive record() here at its native cadence.
        {
            static unsigned long lastShotSample = 0;
            if (millis() - lastShotSample >= SHOT_LOG_SAMPLE_INTERVAL_MS) {
                lastShotSample = millis();
                ShotHistory.record();
            }
        }

        if (ui) {
            ui->loop();
            ui->loopProfiles();
        }
        gm_web_pump(); // service the embedded WebUI HTTP/WS server

        // Settings persistence normally runs in a deferred save task (a no-op in the
        // sim), so flush dirty settings to NVS periodically. save(true) is a cheap
        // no-op when nothing changed.
        {
            static unsigned long lastSave = 0;
            if (millis() - lastSave >= 2000) {
                lastSave = millis();
                controller.getSettings().save(true);
            }
        }

        drv->pumpAndRender();

        if (shotPath && !shotTaken && millis() - start >= shotDelayMs) {
            drv->screenshot(shotPath);
            shotTaken = true;
            break;
        }
        delay(5);
    }
    return 0;
}
