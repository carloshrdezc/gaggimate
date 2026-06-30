#ifndef BREWPROCESS_H
#define BREWPROCESS_H

#include <algorithm>
#include <display/core/constants.h>
#include <display/core/predictive.h>
#include <display/core/process/Process.h>
#include <display/models/profile.h>
#include <esp_log.h>

class BrewProcess : public Process {
  public:
    Profile profile;
    ProcessTarget target;
    // Live scale availability, pushed by Controller each tick; gates the CAR-367
    // terminal-phase duration-cap suppression so a mid-shot scale loss restores
    // the duration fallback. Defaults true to preserve behavior if never set.
    bool volumetricAvailable = true;
    double brewDelay;
    unsigned int phaseIndex = 0;
    Phase currentPhase;
    ProcessPhase processPhase = ProcessPhase::RUNNING;
    unsigned long processStarted = 0;
    unsigned long currentPhaseStarted = 0;
    unsigned long previousPhaseFinished = 0;
    unsigned long finished = 0;
    double currentVolume = 0; // most recent volume pushed
    float currentFlow = 0.0f;
    float currentPressure = 0.0f;
    float waterPumped = 0.0f;
    VolumetricRateCalculator volumetricRateCalculator{PREDICTIVE_TIME};

    explicit BrewProcess(Profile profile, ProcessTarget target, double brewDelay = 0.0)
        : profile(profile), target(target), brewDelay(brewDelay) {
        unsigned long now = millis();
        processStarted = now;
        currentPhaseStarted = now;
        if (this->profile.phases.empty()) {
            ESP_LOGE("BrewProcess", "Refusing to start brew profile with no phases");
            processPhase = ProcessPhase::FINISHED;
            finished = now;
            return;
        }
        currentPhase = this->profile.phases[phaseIndex];
        phaseStartPressure = currentPhase.transition.adaptive ? currentPressure : 0;
        phaseStartFlow = currentPhase.transition.adaptive ? currentFlow : 0;
        computeEffectiveTargetsForCurrentPhase();
    }

    void updateVolume(double volume) override { // called even after the Process is no longer active
        currentVolume = volume;
        if (processPhase != ProcessPhase::FINISHED) { // only store measurements while active
            volumetricRateCalculator.addMeasurement(volume);
        }
    }

    void updatePressure(float pressure) { currentPressure = pressure; }

    void updateFlow(float flow) { currentFlow = flow; }

    void setVolumetricAvailable(bool available) { volumetricAvailable = available; }

    unsigned long getTotalDuration() const { return profile.getTotalDuration() * 1000L; }

    unsigned long getPhaseDuration() const { return static_cast<long>(currentPhase.duration) * 1000L; }

    bool isCurrentPhaseFinished() {
        if (millis() - currentPhaseStarted > BREW_SAFETY_DURATION_MS) {
            return true;
        }
        double volume = currentVolume;
        if (volume > 0.0) {
            double currentRate = volumetricRateCalculator.getRate();
            double predictedAddedVolume = currentRate * brewDelay;
            predictedAddedVolume = std::clamp(predictedAddedVolume, 0.0, 8.0);
            volume = currentVolume + predictedAddedVolume;
        }
        float timeInPhase = static_cast<float>(millis() - currentPhaseStarted) / 1000.0f;
        // CAR-367: only the profile's terminal volumetric phase may treat its
        // volumetric target as the authoritative stop and ignore its duration
        // cap. Intermediate volumetric phases keep their duration cap so they
        // still advance on time (see Phase::isFinished + PR #172 review).
        // Additionally gate on live volumetric availability: if the scale goes
        // unhealthy mid-shot (volumetricAvailable false), restore the duration
        // cap so the phase still advances on time, bounded as before by
        // BREW_SAFETY_DURATION_MS, rather than running to that safety ceiling.
        bool suppressDurationForVolumetric = target == ProcessTarget::VOLUMETRIC && volumetricAvailable &&
                                             static_cast<int>(phaseIndex) == profile.indexOfFinalVolumetricPhase();
        return currentPhase.isFinished(target == ProcessTarget::VOLUMETRIC, volume, timeInPhase, currentFlow, currentPressure,
                                       waterPumped, suppressDurationForVolumetric);
    }

    bool isUtility() const { return profile.utility; }

    double getBrewVolume() const {
        double brewVolume = 0;
        for (const auto &phase : profile.phases) {
            if (phase.hasVolumetricTarget()) {
                Target target = phase.getVolumetricTarget();
                brewVolume = target.value;
            }
        }
        return brewVolume;
    }

    double getNewDelayTime() {
        double newDelay = brewDelay + volumetricRateCalculator.getOvershootAdjustMillis(getBrewVolume(), currentVolume);
        if (newDelay <= 0.0 || newDelay >= PREDICTIVE_TIME) {
            return -1;
        }
        return newDelay;
    }

    bool isRelayActive() override {
        if (processPhase == ProcessPhase::FINISHED) {
            return false;
        }
        return currentPhase.valve;
    }

    bool isAltRelayActive() override { return false; }

    float getPumpValue() override {
        if (processPhase == ProcessPhase::FINISHED) {
            return 0.0f;
        }
        return currentPhase.pumpIsSimple ? currentPhase.pumpSimple : 100.0f;
    }

    bool isAdvancedPump() const { return processPhase != ProcessPhase::FINISHED && !currentPhase.pumpIsSimple; }

    [[nodiscard]] PumpTarget getPumpTarget() const { return currentPhase.pumpAdvanced.target; }

    float getPumpPressure() const {
        if (!isAdvancedPump())
            return 0.0f;
        const float startVal = phaseStartPressure;
        const float endVal = effectivePressure;
        const float a = transitionAlpha();
        return startVal + (endVal - startVal) * a;
    }

    float getPumpFlow() const {
        if (!isAdvancedPump())
            return 0.0f;
        const float startVal = phaseStartFlow;
        const float endVal = effectiveFlow;
        const float a = transitionAlpha();
        return startVal + (endVal - startVal) * a;
    }

    float getTemperature() const {
        if (currentPhase.temperature > 0.0f) {
            return currentPhase.temperature;
        }
        return profile.temperature;
    }

    void progress() override {
        // Progress should be called around every 100ms, as defined in PROGRESS_INTERVAL, while the Process is active
        if (processPhase != ProcessPhase::RUNNING) {
            return;
        }
        if (profile.phases.empty() || phaseIndex >= profile.phases.size()) {
            ESP_LOGE("BrewProcess", "Phase index %u out of bounds for %u phases; finishing brew process", phaseIndex,
                     static_cast<unsigned int>(profile.phases.size()));
            processPhase = ProcessPhase::FINISHED;
            finished = millis();
            return;
        }
        waterPumped += currentFlow / 10.0f; // Add current flow divided to 100ms to water pumped counter
        while (isCurrentPhaseFinished() && processPhase == ProcessPhase::RUNNING) {
            previousPhaseFinished = millis();
            if (phaseIndex + 1 < profile.phases.size()) {
                waterPumped = 0.0f;
                phaseIndex++;
                Phase nextPhase = profile.phases[phaseIndex];
                phaseStartPressure = nextPhase.transition.adaptive ? currentPressure : getPumpPressure();
                phaseStartFlow = nextPhase.transition.adaptive ? currentFlow : getPumpFlow();
                currentPhase = nextPhase;
                currentPhaseStarted = millis();
                computeEffectiveTargetsForCurrentPhase();
            } else {
                processPhase = ProcessPhase::FINISHED;
                finished = millis();
            }
        }
    }

    bool isActive() override { return processPhase == ProcessPhase::RUNNING; }

    bool isComplete() override {
        if (target == ProcessTarget::TIME) {
            return !isActive();
        }
        return processPhase == ProcessPhase::FINISHED && millis() - finished > PREDICTIVE_TIME;
    }

    int getType() override { return MODE_BREW; }

  private:
    float phaseStartPressure = 0.0f;
    float phaseStartFlow = 0.0f;

    float effectivePressure = 0.0f;
    float effectiveFlow = 0.0f;

    static float easeLinear(float t) { return t; }
    static float easeIn(float t) { return t * t; }
    static float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
    static float easeInOut(float t) { return (t < 0.5f) ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t); }

    float applyEasing(float t, TransitionType type) const {
        if (t <= 0.0f)
            return 0.0f;
        if (t >= 1.0f)
            return 1.0f;
        switch (type) {
        case TransitionType::LINEAR:
            return easeLinear(t);
        case TransitionType::EASE_IN:
            return easeIn(t);
        case TransitionType::EASE_OUT:
            return easeOut(t);
        case TransitionType::EASE_IN_OUT:
            return easeInOut(t);
        case TransitionType::INSTANT:
        default:
            return 1.0f;
        }
    }

    void computeEffectiveTargetsForCurrentPhase() {
        if (currentPhase.pumpIsSimple) {
            effectivePressure = 0.0f;
            effectiveFlow = 0.0f;
            return;
        }

        // If the profile requests -1, use the *measured* value at the moment the phase starts.
        effectivePressure =
            (currentPhase.pumpAdvanced.pressure == -1.0f) ? phaseStartPressure : currentPhase.pumpAdvanced.pressure;
        effectiveFlow = (currentPhase.pumpAdvanced.flow == -1.0f) ? phaseStartFlow : currentPhase.pumpAdvanced.flow;
        if (currentPhase.pumpAdvanced.target == PumpTarget::PUMP_TARGET_FLOW) {
            phaseStartPressure = effectivePressure;
        } else {
            phaseStartFlow = effectiveFlow;
        }
    }

    float transitionAlpha() const {
        float dur_s = currentPhase.transition.duration;
        if (dur_s <= 0.0f) {
            dur_s = currentPhase.duration; // If the transition has no duration, use the phase duration
        }
        if (currentPhase.transition.type == TransitionType::INSTANT || dur_s <= 0.0f) {
            return 1.0f;
        }
        const unsigned long elapsedMs = millis() - currentPhaseStarted;
        float t = float(elapsedMs) / (dur_s * 1000.0f);
        return applyEasing(t, currentPhase.transition.type);
    }
};

#endif // BREWPROCESS_H
