#include <display/core/SlogToProfileConverter.h>
#include <display/core/utils.h>

Profile SlogToProfileConverter::convert(const String &slogPath, const String &label, FS *fs) {
    Profile profile;
    profile.label = label;
    profile.type = ProfileType::RECORDED;
    profile.description = "Recorded from manual mode";

    File file = fs->open(slogPath, FILE_READ);
    if (!file) {
        ESP_LOGW("SlogToProfile", "convert failed - could not open file: %s", slogPath.c_str());
        return profile;
    }

    ShotLogHeader header;
    if (file.readBytes(reinterpret_cast<char *>(&header), sizeof(ShotLogHeader)) != sizeof(ShotLogHeader)) {
        file.close();
        ESP_LOGW("SlogToProfile", "convert failed - could not read header from: %s", slogPath.c_str());
        return profile;
    }

    if (header.magic != SHOT_LOG_MAGIC) {
        file.close();
        ESP_LOGW("SlogToProfile", "convert failed - invalid magic in: %s", slogPath.c_str());
        return profile;
    }

    std::vector<float> pressures;
    std::vector<float> flows;
    std::vector<float> temperatures;
    std::vector<int> valves;

    ShotLogSample sample;
    unsigned long lastTick = 0;

    while (file.readBytes(reinterpret_cast<char *>(&sample), sizeof(ShotLogSample)) == sizeof(ShotLogSample)) {
        float tp = sample.tp / 10.0f;
        float fl = sample.fl / 100.0f;
        float ct = sample.ct / 10.0f;
        int vl = 1; // valve state not stored in slog v5 — valve defaults to open for entire shot
        // Note: future slog versions should record valve trajectory to enable
        // precise replay of pre-infusion and other multi-phase valve patterns

        pressures.push_back(tp);
        flows.push_back(fl);
        temperatures.push_back(ct);
        valves.push_back(vl);
        lastTick = sample.t;
    }
    file.close();

    if (pressures.empty()) {
        ESP_LOGW("SlogToProfile", "convert failed - no samples in: %s", slogPath.c_str());
        return profile;
    }

    // Compute average recorded temperature for the profile setpoint
    float tempSum = 0.0f;
    for (float t : temperatures) { tempSum += t; }
    profile.temperature = temperatures.empty() ? 90.0f : (tempSum / temperatures.size());

    Phase phase;
    phase.name = "Recorded Shot";
    phase.phase = PhaseType::PHASE_TYPE_BREW;
    phase.valve = 1;
    phase.duration = lastTick * SHOT_LOG_SAMPLE_INTERVAL_MS / 1000.0f;
    phase.pumpIsSimple = false;
    phase.pumpAdvanced.target = PumpTarget::PUMP_TARGET_PRESSURE;
    phase.pumpAdvanced.pressure = 0;
    phase.pumpAdvanced.flow = 0;
    phase.transition.type = TransitionType::INSTANT;
    phase.transition.duration = 0;
    phase.transition.adaptive = false;

    phase.recordedPressure = pressures;
    phase.recordedFlow = flows;
    phase.recordedTemperature = temperatures;
    phase.recordedValve = valves;
    phase.temperature = profile.temperature;

    profile.phases.push_back(phase);
    profile.id = generateShortID();
    return profile;
}
