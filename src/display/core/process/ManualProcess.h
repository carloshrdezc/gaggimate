#ifndef MANUALPROCESS_H
#define MANUALPROCESS_H

#include <display/core/process/Process.h>
#include <display/core/constants.h>

class ManualProcess : public Process {
  public:
    unsigned long processStarted = 0;
    unsigned long currentPhaseStarted = 0;
    ProcessPhase processPhase = ProcessPhase::RUNNING;
    float livePressure = 9.0f;
    float liveFlow = 2.0f;
    float liveTemperature = 93.0f;
    int liveValve = 1; // 0=closed, 1=open

    explicit ManualProcess() {
        unsigned long now = millis();
        processStarted = now;
        currentPhaseStarted = now;
    }

    void updateLiveValues(float pressure, float flow, float temperature, int valve) {
        if (!isnan(pressure)) livePressure = pressure;
        if (!isnan(flow)) liveFlow = flow;
        if (!isnan(temperature)) liveTemperature = temperature;
        if (!isnan(valve)) liveValve = valve;
    }

    void updatePressure(float p) { livePressure = p; }
    void updateFlow(float f) { liveFlow = f; }

    bool isRelayActive() override {
        return isActive() && liveValve == 1;
    }

    bool isAltRelayActive() override { return false; }

    float getPumpValue() override { return isActive() ? 100.f : 0.f; }

    bool isAdvancedPump() { return isActive(); }

    PumpTarget getPumpTarget() { return PumpTarget::PUMP_TARGET_PRESSURE; }

    float getPumpPressure() { return isActive() ? livePressure : 0.f; }

    float getPumpFlow() { return isActive() ? liveFlow : 0.f; }

    float getTemperature() { return liveTemperature; }
    void setTemperature(float t) { liveTemperature = t; }

    void progress() override {
        // Stateless — driven entirely by live values set via updateLiveValues()
        // Safety check: enforce max brew duration
        if (millis() - processStarted > BREW_SAFETY_DURATION_MS) {
            processPhase = ProcessPhase::FINISHED;
        }
    }

    bool isActive() override { return processPhase == ProcessPhase::RUNNING; }

    bool isComplete() override { return !isActive(); }

    int getType() override { return MODE_MANUAL; }

    void updateVolume(double volume) override {
        // Not used in manual mode — volumetric tracking handled by ShotHistoryPlugin
    }
};

#endif // MANUALPROCESS_H
