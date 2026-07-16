#include "ShotHistoryPlugin.h"
#include "ActiveShotFillPolicy.h"

#include "BeanResolutionPolicy.h"
#include "ExtendedRecordingPolicy.h"
#include "ShotIndexMetadataPolicy.h"
#include "ShotNotesPersistencePolicy.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <algorithm>
#include <cmath>
#include <display/core/Controller.h>
#include <display/core/EventIds.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/utils.h>
#include <display/models/shot_log_format.h>

namespace {
constexpr float TEMP_SCALE = 10.0f;
constexpr float PRESSURE_SCALE = 10.0f;
constexpr float FLOW_SCALE = 100.0f;
constexpr float WEIGHT_SCALE = 10.0f;
constexpr float RESISTANCE_SCALE = 100.0f;

constexpr uint16_t TEMP_MAX_VALUE = 2000;    // 200.0 °C
constexpr uint16_t PRESSURE_MAX_VALUE = 200; // 20.0 bar
constexpr uint16_t WEIGHT_MAX_VALUE = 10000; // 1000.0 g
constexpr uint16_t RESISTANCE_MAX_VALUE = 0xFFFF;
constexpr int16_t FLOW_MIN_VALUE = -2000; // -20.00 ml/s
constexpr int16_t FLOW_MAX_VALUE = 2000;  //  20.00 ml/s

// Minimum duration for a valid shot (7.5 seconds)
// Shots shorter than this are considered failed/flushes and are excluded
constexpr unsigned long MIN_VALID_SHOT_DURATION_MS = 7500;

// PRO-441: fill-only-if-absent stamp of a device-recorded value onto shot notes. Returns true
// when it wrote (so the caller can OR into notesChanged). Deduped from four near-identical
// inline stamps (beanType/beanId/grinderName/grindTarget) so the JsonVariant isNull/isEmpty +
// assignment machinery is emitted ONCE, not per call site — this keeps flash usage down on
// size-constrained firmware builds (PRO-427/441). The parameter is a raw
// `const char *` (callers pass String::c_str() or a stack buffer) so no String temporary is
// copy-constructed at any call site; ArduinoJson copies the value into the document pool, so a
// pointer into caller-owned storage is safe. `noexcept` drops the copy exception unwind tables.
// A user-entered value is never clobbered.
bool stampNoteIfAbsent(JsonDocument &notes, const char *key, const char *value) noexcept {
    // Treat an existing value as "present" only when it is a non-empty string. Read it as a raw
    // const char* (points into the document pool — no String temporary/dtor/unwind emitted here) so
    // this helper stays small; it is the flash-budget-sensitive path (PRO-441).
    const char *existing = notes[key].as<const char *>();
    if (!value || !*value || (existing && *existing)) {
        return false;
    }
    notes[key] = value;
    return true;
}

uint16_t encodeUnsigned(float value, float scale, uint16_t maxValue) {
    if (!std::isfinite(value)) {
        return 0;
    }
    float scaled = value * scale;
    if (scaled < 0.0f) {
        scaled = 0.0f;
    }
    scaled += 0.5f;
    uint32_t fixed = static_cast<uint32_t>(scaled);
    if (fixed > maxValue) {
        fixed = maxValue;
    }
    return static_cast<uint16_t>(fixed);
}

int16_t encodeSigned(float value, float scale, int16_t minValue, int16_t maxValue) {
    if (!std::isfinite(value)) {
        return 0;
    }
    float scaled = value * scale;
    if (scaled >= 0.0f) {
        scaled += 0.5f;
    } else {
        scaled -= 0.5f;
    }
    int32_t fixed = static_cast<int32_t>(scaled);
    if (fixed < minValue) {
        fixed = minValue;
    }
    if (fixed > maxValue) {
        fixed = maxValue;
    }
    return static_cast<int16_t>(fixed);
}

String padId(uint32_t id, int length = 10) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%0*lu", length, (unsigned long)id);
    return String(buffer);
}

String padId(const String &id, int length = 10) { return padId((uint32_t)id.toInt(), length); }

float parseDoseValue(JsonVariantConst value) {
    if (value.isNull()) {
        return 0.0f;
    }

    float parsed = 0.0f;
    if (value.is<float>() || value.is<double>() || value.is<int>()) {
        parsed = value.as<float>();
    } else {
        parsed = value.as<String>().toFloat();
    }

    return std::isfinite(parsed) && parsed > 0.0f ? parsed : 0.0f;
}

// PRO-423: build the lightweight (id, name) view the pure BeanResolutionPolicy
// consumes. Shared by both resolution call sites (read-time findBeanIndexForNotes
// and capture-time resolveSelectedBeanId) so the BeanEntry -> BeanRef marshalling
// lives in exactly one place.
std::vector<bean_resolution::BeanRef> refsFromBeans(const std::vector<BeanEntry> &beans) {
    std::vector<bean_resolution::BeanRef> refs;
    refs.reserve(beans.size());
    for (const auto &bean : beans) {
        refs.push_back({std::string(bean.id.c_str()), std::string(bean.name.c_str())});
    }
    return refs;
}

int findBeanIndexForNotes(JsonVariantConst notes, const std::vector<BeanEntry> &beans) {
    // PRO-422: id-first, name-second resolution lives in the pure, host-testable
    // BeanResolutionPolicy so the device and the native unit tests share one
    // matching order. Build a lightweight (id, name) view over the beans vector.
    const std::vector<bean_resolution::BeanRef> refs = refsFromBeans(beans);
    const std::string beanId = std::string(notes["beanId"].as<String>().c_str());
    const std::string beanType = std::string(notes["beanType"].as<String>().c_str());
    return bean_resolution::resolveBeanIndex(beanId, beanType, refs);
}

float roundBeanQuantity(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return roundf(value * 100.0f) / 100.0f;
}

// PRO-277: RAII guard for the index mutex. Blocks (portMAX_DELAY) because index
// operations are infrequent (shot start/end, notes save, rebuild) and must not be
// silently skipped the way a timed-out telemetry sample can be — a lost index write
// is a corrupted/missing shot record. If the mutex was never created (setup failure)
// the guard is a no-op and the caller proceeds unlocked rather than deadlocking.
//
// The no-op is safe in that degraded-boot state: setup() returns early (L177-180)
// when indexMutex creation fails and therefore never spawns the loopTask, so the
// loopTask's index writers (record() -> createEarlyIndexEntry / appendCompletedShotToIndex,
// cleanupHistory -> markIndexDeleted) can never run. The WebUI request path
// (WebUIPlugin -> handleRequest -> updateIndexMetadata / markIndexDeleted) and the
// async-rebuild task are NOT gated on setup() success, so they remain reachable —
// but they are still mutually serialized without the mutex: the WebSocket handler
// runs on the single AsyncTCP relay task (one request at a time), and a heap so
// exhausted that xSemaphoreCreateMutex() failed will also fail the rebuild task's
// xTaskCreatePinnedToCore(), so no second concurrent index writer materializes.
// Hence the unlocked fallback cannot interleave two index writers in practice.
class IndexLockGuard {
  public:
    explicit IndexLockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_ != nullptr) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            held_ = true;
        }
    }
    ~IndexLockGuard() {
        if (held_) {
            xSemaphoreGive(mutex_);
        }
    }
    IndexLockGuard(const IndexLockGuard &) = delete;
    IndexLockGuard &operator=(const IndexLockGuard &) = delete;

  private:
    SemaphoreHandle_t mutex_;
    bool held_ = false;
};
} // namespace

ShotHistoryPlugin ShotHistory;

void ShotHistoryPlugin::setup(Controller *c, PluginManager *pm) {
    controller = c;
    pluginManager = pm;
    if (controller->isSDCard()) {
        fs = &SD_MMC;
        ESP_LOGI("ShotHistoryPlugin", "Logging shot history to SD card");
    }

    // Create mutex for thread-safe access to shared state
    stateMutex = xSemaphoreCreateMutex();
    if (stateMutex == nullptr) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create state mutex");
        return;
    }

    recordingMutex = xSemaphoreCreateMutex();
    if (recordingMutex == nullptr) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create recording mutex");
        return;
    }

    // Notes requests can perform LittleFS/SD I/O. Keep them serialized separately
    // from the recording state lock so live measurement callbacks never wait for
    // storage.
    notesMutex = xSemaphoreCreateMutex();
    if (notesMutex == nullptr) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create notes mutex");
        return;
    }

    // PRO-277: serialize all /h/index.bin operations across tasks.
    indexMutex = xSemaphoreCreateMutex();
    if (indexMutex == nullptr) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create index mutex");
        return;
    }

    pm->on(EventIds::CONTROLLER_BREW_START, [this](Event const &) { startRecording(); });
    pm->on(EventIds::CONTROLLER_BREW_END, [this](Event const &) { endRecording(); });
    pm->on(EventIds::CONTROLLER_BREW_CLEAR, [this](Event const &) { endExtendedRecording(); });
    pm->on(EventIds::CONTROLLER_PROCESS_START, [this](Event const &) {
        if (controller != nullptr && controller->getProcessType() == MODE_MANUAL) {
            startRecording();
        }
    });
    pm->on(EventIds::CONTROLLER_PROCESS_END, [this](Event const &event) {
        if (event.getInt("processType") == MODE_MANUAL) {
            endRecording(false);
        }
    });
    pm->on(EventIds::CONTROLLER_VOLUMETRIC_MEASUREMENT_ESTIMATION_CHANGE, [this](Event const &event) {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            currentEstimatedWeight = event.getFloat("value");
            xSemaphoreGive(stateMutex);
        } else {
            ESP_LOGW("ShotHistoryPlugin", "Failed to acquire mutex for estimation weight update");
        }
    });
    pm->on(EventIds::CONTROLLER_VOLUMETRIC_MEASUREMENT_BLUETOOTH_CHANGE, [this](Event const &event) {
        // PRO-367: coalesce-latest instead of drop-on-timeout so the recorded
        // yield matches the settled weight. The scale weight is monotonic
        // cumulative, so latch the freshest value and apply it (plus any value a
        // prior timed-out take latched) on success.
        bluetoothWeightCoalescer.latch(event.getFloat("value"));
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            double latest = 0.0;
            if (bluetoothWeightCoalescer.consumeInto(latest)) {
                currentBluetoothWeight = static_cast<float>(latest);
            }
            xSemaphoreGive(stateMutex);
        } else {
            ESP_LOGW("ShotHistoryPlugin", "Failed to acquire mutex for bluetooth weight update");
        }
    });
    pm->on(EventIds::BOILER_CURRENT_TEMPERATURE_CHANGE, [this](Event const &event) {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            currentTemperature = event.getFloat("value");
            xSemaphoreGive(stateMutex);
        } else {
            ESP_LOGW("ShotHistoryPlugin", "Failed to acquire mutex for temperature update");
        }
    });
    pm->on(EventIds::PUMP_PUCK_RESISTANCE_CHANGE, [this](Event const &event) {
        if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            currentPuckResistance = event.getFloat("value");
            xSemaphoreGive(stateMutex);
        } else {
            ESP_LOGW("ShotHistoryPlugin", "Failed to acquire mutex for puck resistance update");
        }
    });
    // Initialize rebuild state
    rebuildInProgress = false;

    // Only create task if mutex was successfully created
    if (stateMutex != nullptr) {
        xTaskCreatePinnedToCore(loopTask, "ShotHistoryPlugin::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle, 0);
    }
}
bool ShotHistoryPlugin::openLogFileIfNeeded() {
    if (isFileOpen) {
        return true;
    }

    if (!fs->exists("/h")) {
        if (!fs->mkdir("/h")) {
            ESP_LOGE("ShotHistoryPlugin", "Failed to create history directory");
            return false;
        }
    }

    currentFile = fs->open("/h/" + currentId + ".slog", FILE_WRITE);
    if (!currentFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to open shot log file for writing");
        return false;
    }

    isFileOpen = true;
    initializeHeader();
    size_t written = currentFile.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
    if (written != sizeof(header)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to write header: expected %zu, wrote %zu", sizeof(header), written);
        currentFile.close();
        isFileOpen = false;
        return false;
    }
    return true;
}

void ShotHistoryPlugin::initializeHeader() {
    memset(&header, 0, sizeof(header));
    header.magic = SHOT_LOG_MAGIC;
    header.version = SHOT_LOG_VERSION;
    header.reserved0 = (uint8_t)SHOT_LOG_SAMPLE_SIZE;
    header.headerSize = SHOT_LOG_HEADER_SIZE;
    header.sampleInterval = SHOT_LOG_SAMPLE_INTERVAL_MS;
    header.fieldsMask = SHOT_LOG_FIELDS_MASK_ALL;
    header.startEpoch = getTime();
    header.phaseTransitionCount = 0;

    if (!controller) {
        ESP_LOGE("ShotHistoryPlugin", "Controller is null in initializeHeader");
        return;
    }

    if (controller->getMode() == MODE_MANUAL || controller->getProcessType() == MODE_MANUAL) {
        strncpy(header.profileId, "manual", sizeof(header.profileId) - 1);
        header.profileId[sizeof(header.profileId) - 1] = '\0';
        strncpy(header.profileName, "Manual", sizeof(header.profileName) - 1);
        header.profileName[sizeof(header.profileName) - 1] = '\0';
        return;
    }

    Profile profile = controller->getProfileManager()->getSelectedProfile();
    strncpy(header.profileId, profile.id.c_str(), sizeof(header.profileId) - 1);
    header.profileId[sizeof(header.profileId) - 1] = '\0';
    strncpy(header.profileName, profile.label.c_str(), sizeof(header.profileName) - 1);
    header.profileName[sizeof(header.profileName) - 1] = '\0';
}

ShotLogSample ShotHistoryPlugin::createSample(float bluetoothWeight, float estimatedWeight, float temperature,
                                              float puckResistance) {
    ShotLogSample sample{};

    if (!controller) {
        ESP_LOGE("ShotHistoryPlugin", "Controller is null in createSample");
        return sample;
    }

    uint32_t tick = sampleCount <= 0xFFFF ? sampleCount : 0xFFFF;

    sample.t = static_cast<uint16_t>(tick);
    sample.tt = encodeUnsigned(controller->getTargetTemp(), TEMP_SCALE, TEMP_MAX_VALUE);
    sample.ct = encodeUnsigned(temperature, TEMP_SCALE, TEMP_MAX_VALUE);
    sample.tp = encodeUnsigned(controller->getTargetPressure(), PRESSURE_SCALE, PRESSURE_MAX_VALUE);
    sample.cp = encodeUnsigned(controller->getCurrentPressure(), PRESSURE_SCALE, PRESSURE_MAX_VALUE);
    sample.fl = encodeSigned(controller->getCurrentPumpFlow(), FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
    sample.tf = encodeSigned(controller->getTargetFlow(), FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
    sample.pf = encodeSigned(controller->getCurrentPuckFlow(), FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
    sample.vf = encodeSigned(currentBluetoothFlow, FLOW_SCALE, FLOW_MIN_VALUE, FLOW_MAX_VALUE);
    sample.v = encodeUnsigned(bluetoothWeight, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
    sample.ev = encodeUnsigned(estimatedWeight, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
    sample.pr = encodeUnsigned(puckResistance, RESISTANCE_SCALE, RESISTANCE_MAX_VALUE);
    sample.si = getSystemInfo();

    return sample;
}

void ShotHistoryPlugin::updateBluetoothFlow(float bluetoothWeight) {
    static constexpr float BLUETOOTH_FLOW_SAMPLE_INTERVAL = 0.25f;  // 250ms sample interval
    static constexpr float BLUETOOTH_FLOW_SMOOTHING_FACTOR = 0.25f; // 25% new, 75% old

    float btDiff = bluetoothWeight - lastBluetoothWeight;
    float btFlow = btDiff / BLUETOOTH_FLOW_SAMPLE_INTERVAL;
    currentBluetoothFlow =
        currentBluetoothFlow * (1.0f - BLUETOOTH_FLOW_SMOOTHING_FACTOR) + btFlow * BLUETOOTH_FLOW_SMOOTHING_FACTOR;
    lastBluetoothWeight = bluetoothWeight;
}

bool ShotHistoryPlugin::writeSampleToBuffer(const ShotLogSample &sample) {
    if (!isFileOpen) {
        return false;
    }

    if (ioBufferPos + sizeof(sample) > sizeof(ioBuffer)) {
        if (!flushBuffer()) {
            ESP_LOGE("ShotHistoryPlugin", "Failed to flush buffer, stopping recording");
            isFileOpen = false;
            currentFile.close();
            return false;
        }
    }

    memcpy(ioBuffer + ioBufferPos, &sample, sizeof(sample));
    ioBufferPos += sizeof(sample);
    sampleCount++;
    return true;
}

void ShotHistoryPlugin::checkEarlyIndexCreation() {
    if (!indexEntryCreated && (millis() - shotStart) > MIN_VALID_SHOT_DURATION_MS) {
        indexEntryCreated = createEarlyIndexEntry();
    }
}

bool ShotHistoryPlugin::closeLogFile(float finalBluetoothWeight) {
    if (!isFileOpen) {
        return false;
    }

    if (!flushBuffer()) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to flush final buffer, marking shot as failed");
        currentFile.close();
        isFileOpen = false;
        return false;
    }

    patchHeaderWithFinalData(finalBluetoothWeight);
    currentFile.close();
    isFileOpen = false;

    if (isShotTooShort()) {
        return false;
    } else {
        return true;
    }
}

void ShotHistoryPlugin::patchHeaderWithFinalData(float finalBluetoothWeight) {
    header.sampleCount = sampleCount;
    header.durationMs = millis() - shotStart;
    header.finalWeight = finalBluetoothWeight > 0.0f ? encodeUnsigned(finalBluetoothWeight, WEIGHT_SCALE, WEIGHT_MAX_VALUE) : 0;

    currentFile.seek(0, SeekSet);
    currentFile.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
}

bool ShotHistoryPlugin::isShotTooShort() const { return header.durationMs <= MIN_VALID_SHOT_DURATION_MS; }

void ShotHistoryPlugin::handleFailedShot(const String &id, bool hadIndexEntry) {
    removeHistoryFiles(id);

    if (hadIndexEntry) {
        markIndexDeleted(id.toInt());
    }
}

void ShotHistoryPlugin::handleCompletedShot(const String &id, const String &beanName, const String &beanId,
                                            const String &grinderName, bool startedVolumetric, double grindTargetVolume,
                                            int grindTargetDuration, const ShotLogHeader &completedHeader) {
    if (!controller) {
        ESP_LOGE("ShotHistoryPlugin", "Controller is null in handleCompletedShot");
        return;
    }

    controller->getSettings().setHistoryIndex(controller->getSettings().getHistoryIndex() + 1);

    if (notesMutex == nullptr || xSemaphoreTake(notesMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW("ShotHistoryPlugin", "Failed to acquire notes mutex for completed shot");
        cleanupHistory();
        appendCompletedShotToIndex(id, completedHeader);
        return;
    }

    // Auto-save bean name to notes, merging with any dose/notes the WebUI may have already written.
    // Serialize with WebUI note writes so its load/merge/save cannot clobber them.
    // Track whether a notes file exists so appendCompletedShotToIndex can set SHOT_FLAG_HAS_NOTES
    // without an extra fs->exists() stat call.
    bool hasNotes = false;
    // PRO-441: a shot with only a machine grind TARGET (no bean/grinder selected) must still write notes.
    bool hasGrindTarget = (startedVolumetric ? grindTargetVolume > 0 : grindTargetDuration > 0);
    if (!beanName.isEmpty() || !grinderName.isEmpty() || hasGrindTarget) {
        JsonDocument previousNotes;
        JsonDocument autoNotes;
        loadNotes(id, previousNotes);
        loadNotes(id, autoNotes);
        // PRO-422/428/441: stamp device-recorded fields fill-only-if-absent (never clobber a
        // user-entered value). beanType/beanId (PRO-422), grinderName (PRO-428, the contract
        // PRO-430's isGrinderRecordedForShot keys off; no grinder-id analog — name-only in
        // Settings), and grindTarget (PRO-441) all route through stampNoteIfAbsent so every web
        // client reads the same authoritative values.
        bool notesChanged = false;
        notesChanged |= stampNoteIfAbsent(autoNotes, "beanType", beanName.c_str());
        notesChanged |= stampNoteIfAbsent(autoNotes, "beanId", beanId.c_str());
        notesChanged |= stampNoteIfAbsent(autoNotes, "grinderName", grinderName.c_str());
        // PRO-441: the machine grind TARGET is DISTINCT from the user-editable "grindSetting" dial
        // — grindTarget is "what the machine was set to auto-grind" (device-authoritative). The
        // label matches ProcessControls.jsx formatTarget() EXACTLY: volumetric -> "18.0g"
        // (formatNumber = toFixed(1)), time -> "25s" (Math.round(ms/1000)). Only built when present.
        if (hasGrindTarget) {
            // Format via a single snprintf call site (integer math, no float->String/dtostrf
            // machinery) to match ProcessControls.jsx formatTarget() EXACTLY: volumetric -> "18.0g"
            // (toFixed(1)), time -> "25s" (Math.round(ms/1000)). Both cases feed the same call; the
            // time format "%lds" simply ignores the trailing fractional arg (valid in C). Collapsing
            // to one snprintf (vs one per branch) trims the last bytes for the 2 MB partition.
            char gt[16];
            long whole;
            long frac = 0;
            const char *fmt;
            if (startedVolumetric) {
                // Tenths of a gram. tgv is entered in 0.1g steps and is positive here (guarded by
                // hasGrindTarget), so (long)(v*10 + 0.5) is round-half-up == JS toFixed(1) across the
                // reachable value domain, and avoids pulling lround (this feature is its only user).
                long tenths = static_cast<long>(grindTargetVolume * 10.0 + 0.5);
                whole = tenths / 10;
                frac = tenths % 10;
                fmt = "%ld.%ldg";
            } else {
                // Integer round-half-up: currentGrindTargetDuration is non-negative ms (guarded by
                // hasGrindTarget), so (ms + 500) / 1000 == JS Math.round(ms/1000) exactly.
                whole = (grindTargetDuration + 500) / 1000;
                fmt = "%lds";
            }
            snprintf(gt, sizeof(gt), fmt, whole, frac);
            notesChanged |= stampNoteIfAbsent(autoNotes, "grindTarget", gt);
        }
        if (notesChanged) {
            hasNotes = saveNotes(id, autoNotes);
            if (hasNotes && !applyBeanUsageDelta(previousNotes, autoNotes)) {
                ESP_LOGW("ShotHistoryPlugin", "Failed to update bean usage for completed shot %s", currentId.c_str());
            }
        } else {
            hasNotes = true; // WebUI already wrote notes that include beanType/beanId/grinderName
        }
    } else {
        // No bean, grinder, or grind target — WebUI may still have written dose-only notes.
        hasNotes = fs->exists("/h/" + id + ".json");
    }

    xSemaphoreGive(notesMutex);
    // cleanupHistory() serializes each sidecar removal with notesMutex, so it
    // must run only after the completion read/merge/write transaction releases it.
    cleanupHistory();
    appendCompletedShotToIndex(id, completedHeader, hasNotes);
}

void ShotHistoryPlugin::appendCompletedShotToIndex(const String &id, const ShotLogHeader &completedHeader, bool hasNotes) {
    ShotIndexEntry indexEntry{};
    indexEntry.id = id.toInt();
    indexEntry.timestamp = completedHeader.startEpoch;
    indexEntry.duration = completedHeader.durationMs;
    indexEntry.volume = completedHeader.finalWeight;
    indexEntry.rating = 0;
    indexEntry.flags = SHOT_FLAG_COMPLETED;
    if (hasNotes) {
        indexEntry.flags |= SHOT_FLAG_HAS_NOTES;
    }
    strncpy(indexEntry.profileId, completedHeader.profileId, sizeof(indexEntry.profileId) - 1);
    indexEntry.profileId[sizeof(indexEntry.profileId) - 1] = '\0';
    strncpy(indexEntry.profileName, completedHeader.profileName, sizeof(indexEntry.profileName) - 1);
    indexEntry.profileName[sizeof(indexEntry.profileName) - 1] = '\0';

    if (!appendToIndex(indexEntry)) {
        ESP_LOGE("ShotHistoryPlugin", "CRITICAL: Failed to add completed shot %u to index", indexEntry.id);
    }
}

void ShotHistoryPlugin::record() {
    // PRO-277: stateMutex protects the cross-task scalar telemetry
    // (currentBluetoothWeight / currentEstimatedWeight / currentTemperature /
    // currentPuckResistance, written by the event callbacks ~167-192) and the
    // few settle-window scalars shared with startRecording()/endRecording().
    //
    // Field ownership while a shot is ACTIVELY recording (shouldRecord == true):
    //   - loopTask-exclusive (this task only touches them; no other task reads or
    //     writes them once a shot is open): currentFile, header, ioBuffer, isFileOpen.
    //   - start-reset fields written by startRecording()/endRecording() on the
    //     controller task BEFORE the shot opens, then read here: currentId,
    //     currentBeanName, currentProfileName, sampleCount, ioBufferPos,
    //     lastRecordedPhase. These are reset under stateMutex by startRecording()
    //     and are not mutated again by another task while shouldRecord stays true,
    //     so the lock-free sample path below is safe for the active-shot case.
    //
    // The blocking SD-card I/O (openLogFileIfNeeded / writeSampleToBuffer's
    // flushBuffer) used to run while this lock was held, so a 4 KB SD flush
    // (tens-to-hundreds of ms) made the 100 ms-timeout event callbacks drop live
    // samples. We snapshot the shared scalars under the lock, release it, then do
    // the active-shot file work lock-free — the callbacks now only ever contend
    // with a memcpy-speed critical section and stop timing out.
    //
    // The close path needs mutual exclusion with a concurrent startRecording()
    // because both touch currentFile. recordingMutex provides that exclusion while
    // stateMutex is released before the close and completion filesystem work, so
    // telemetry callbacks never wait for Dashboard notes I/O.
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }

    bool shouldRecord = recording || extendedRecording;

    // Snapshot the live telemetry scalars while we hold the lock.
    const float snapBluetoothWeight = currentBluetoothWeight;
    const float snapEstimatedWeight = currentEstimatedWeight;
    const float snapTemperature = currentTemperature;
    const float snapPuckResistance = currentPuckResistance;

    // Take a complete end-of-shot snapshot before closing the file. The state
    // lock protects only the handoff to the completion path; no notes I/O runs
    // under it, so a Dashboard fill cannot block recording completion.
    if (!shouldRecord) {
        const String completedId = currentId;
        const String completedBeanName = currentBeanName;
        const String completedBeanId = currentBeanId;
        const String completedGrinderName = currentGrinderName;
        const bool completedStartedVolumetric = shotStartedVolumetric;
        const double completedGrindTargetVolume = currentGrindTargetVolume;
        const int completedGrindTargetDuration = currentGrindTargetDuration;
        const bool completedHadIndexEntry = indexEntryCreated;
        if (isFileOpen) {
            if (recordingMutex == nullptr || xSemaphoreTake(recordingMutex, portMAX_DELAY) != pdTRUE) {
                xSemaphoreGive(stateMutex);
                return;
            }
            xSemaphoreGive(stateMutex);
            const bool completed = closeLogFile(snapBluetoothWeight);
            const ShotLogHeader completedHeader = header;
            xSemaphoreGive(recordingMutex);
            if (completed) {
                handleCompletedShot(completedId, completedBeanName, completedBeanId, completedGrinderName,
                                    completedStartedVolumetric, completedGrindTargetVolume, completedGrindTargetDuration,
                                    completedHeader);
            } else {
                handleFailedShot(completedId, completedHadIndexEntry);
            }
            return;
        }
        xSemaphoreGive(stateMutex);
        return;
    }

    xSemaphoreGive(stateMutex);

    // ---- Everything below runs WITHOUT stateMutex (active-shot path) ----

    // Only record during brew mode or extended recording
    if (!controller || ((controller->getMode() != MODE_BREW && controller->getMode() != MODE_MANUAL) && !extendedRecording)) {
        return;
    }

    // Open log file if needed
    if (!openLogFileIfNeeded()) {
        // Trigger error event to notify user
        if (pluginManager) {
            Event errorEvent;
            errorEvent.id = EventIds::EVT_SHOT_RECORDING_ERROR;
            errorEvent.setString("error", "Failed to open shot log file");
            pluginManager->trigger(errorEvent);
        }
        return;
    }

    // Update bluetooth flow calculation from the snapshot.
    updateBluetoothFlow(snapBluetoothWeight);

    // Create and write sample from the snapshotted telemetry.
    ShotLogSample sample = createSample(snapBluetoothWeight, snapEstimatedWeight, snapTemperature, snapPuckResistance);

    // Track phase transitions - use thread-safe method to avoid race condition
    if (controller->getMode() == MODE_BREW && controller->getProcessType() == MODE_BREW) {
        uint8_t currentPhase = controller->getBrewProcessPhaseIndex();

        if (currentPhase != lastRecordedPhase) {
            recordPhaseTransition(currentPhase, sampleCount);
            lastRecordedPhase = currentPhase;
        }
    }

    // Write sample to buffer (may flush to SD — now lock-free)
    if (!writeSampleToBuffer(sample)) {
        return;
    }

    // Check for early index creation
    checkEarlyIndexCreation();

    // Check for weight stabilization during extended recording
    if (extendedRecording) {
        const unsigned long now = millis();

        // PRO-232: Mirror the window-open guard in endRecording() — keep settling only
        // while the BLE scale that opened this window is still delivering measurements.
        // (On non-NIGHTLY builds isBluetoothScaleHealthy() == isVolumetricAvailable();
        // on NIGHTLY builds isVolumetricAvailable() can stay true via dimming capability
        // even after the scale dies, which would hold the window open with stale weight
        // until the EXTENDED_RECORDING_DURATION cap.)
        bool canProcessWeight = (controller != nullptr);
        if (canProcessWeight) {
            canProcessWeight = controller->isBluetoothScaleHealthy();
        }

        if (!canProcessWeight) {
            extendedRecording = false;
            return;
        }

        // The settle scalars (lastStableWeight / lastWeightChangeTime /
        // extendedRecordingStart) are shared with start/endRecording(), so read and
        // update them under the lock. weightDiff is computed from the same snapshot
        // used for the sample.
        if (stateMutex != nullptr && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            const float weightDiff = fabsf(snapBluetoothWeight - lastStableWeight);

            if (weightDiff < WEIGHT_STABILIZATION_THRESHOLD) {
                if (lastWeightChangeTime == 0) {
                    lastWeightChangeTime = now;
                }
                if (now - lastWeightChangeTime >= WEIGHT_STABILIZATION_TIME) {
                    extendedRecording = false;
                }
            } else {
                lastWeightChangeTime = 0;
                lastStableWeight = snapBluetoothWeight;
            }

            if (now - extendedRecordingStart >= EXTENDED_RECORDING_DURATION) {
                extendedRecording = false;
            }
            xSemaphoreGive(stateMutex);
        }
    }
}

String ShotHistoryPlugin::resolveSelectedBeanId(const String &beanName) {
    if (beanName.isEmpty() || !controller) {
        return "";
    }
    BeanManager *manager = controller->getBeanManager();
    if (!manager) {
        return "";
    }
    // Reuse the shared id-first/name-second resolver so the capture-time lookup
    // and the read-time findBeanIndexForNotes agree on name normalization.
    std::vector<BeanEntry> beans = manager->listBeans();
    const std::vector<bean_resolution::BeanRef> refs = refsFromBeans(beans);
    const int idx = bean_resolution::resolveBeanIndex("", std::string(beanName.c_str()), refs);
    return idx >= 0 ? beans[static_cast<size_t>(idx)].id : String("");
}

void ShotHistoryPlugin::startRecording() {
    // Identity transitions use stateMutex only. Dashboard fills observe the
    // atomic generation and recheck it after notes I/O, so no lock inversion is
    // possible with the completion path.
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW("ShotHistoryPlugin", "Failed to acquire mutex for startRecording");
        return;
    }

    if (recordingMutex == nullptr || xSemaphoreTake(recordingMutex, portMAX_DELAY) != pdTRUE) {
        xSemaphoreGive(stateMutex);
        ESP_LOGW("ShotHistoryPlugin", "Failed to acquire recording mutex for startRecording");
        return;
    }

    if (!controller) {
        xSemaphoreGive(recordingMutex);
        xSemaphoreGive(stateMutex);
        return;
    }

    // Use thread-safe method to check process type and utility status
    if (controller->getProcessType() == MODE_BREW && controller->isBrewProcessUtility()) {
        xSemaphoreGive(recordingMutex);
        xSemaphoreGive(stateMutex);
        return;
    }
    activeShotId.store(0, std::memory_order_release);
    shotGeneration.fetch_add(1, std::memory_order_relaxed);
    currentId = padId((uint32_t)getTime());
    shotStart = millis();
    lastWeightChangeTime = 0;
    extendedRecordingStart = 0;
    currentBluetoothWeight = 0.0f;
    lastStableWeight = 0.0f;
    currentEstimatedWeight = 0.0f;
    currentBluetoothFlow = 0.0f;
    currentProfileName =
        controller->getProcessType() == MODE_MANUAL ? "Manual" : controller->getProfileManager()->getSelectedProfile().label;
    currentBeanName = controller->getSettings().getSelectedBean();
    // PRO-422: the device stores only the selected bean NAME (req:beans:select
    // carries no id), so resolve name -> stable BeanManager id here at capture
    // time. This id is written durably onto the shot notes at shot end so every
    // web client reads the same authoritative bean instead of guessing per
    // browser. Empty when no bean is selected or the name no longer matches a
    // stored bean (older/deleted bean) — the shot then carries name-only, as before.
    currentBeanId = resolveSelectedBeanId(currentBeanName);
    // PRO-428: capture the selected grinder NAME at brew start, mirroring the bean
    // capture above. Settings stores only the name (req:grinders:select carries no
    // id), so there is no grinder-id analog to beanId; name-only is the payload.
    currentGrinderName = controller->getSettings().getSelectedGrinder();
    recording = true;
    activeShotId.store(currentId.toInt(), std::memory_order_release);
    extendedRecording = false;
    indexEntryCreated = false; // Reset flag for new shot
    sampleCount = 0;
    ioBufferPos = 0;

    // Reset phase tracking for new shot
    lastRecordedPhase = 0xFF; // Invalid value to detect first phase

    // Capture initial volumetric mode state (brew by weight vs brew by time)
    shotStartedVolumetric = controller->getSettings().isVolumetricTarget();

    // PRO-441: snapshot the machine grind TARGET at brew start, mirroring the grinderName capture above.
    // These are read authoritatively across clients by stamping notes "grindTarget" at shot end. The
    // volumetric-vs-time mode is shotStartedVolumetric (captured just above from the same call).
    currentGrindTargetVolume = controller->getSettings().getTargetGrindVolume();
    currentGrindTargetDuration = controller->getSettings().getTargetGrindDuration();

    xSemaphoreGive(recordingMutex);
    xSemaphoreGive(stateMutex);
}

unsigned long ShotHistoryPlugin::getTime() {
    time_t now;
    time(&now);
    return now;
}

void ShotHistoryPlugin::endRecording(bool allowExtendedRecording) {
    // Identity transitions use stateMutex only; Dashboard fills use a generation
    // token plus post-I/O recheck instead of nesting notesMutex and stateMutex.
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW("ShotHistoryPlugin", "Failed to acquire mutex for endRecording");
        return;
    }

    // PRO-232: Open the post-stop settle window whenever a live BLE scale was the
    // active volumetric source at brew-end, gating on isBluetoothScaleHealthy()
    // rather than the instantaneous currentBluetoothWeight sample.
    //
    // The old `currentBluetoothWeight > 0` precondition was too brittle: startRecording()
    // resets currentBluetoothWeight to 0 (line ~549) and it is only refreshed by the
    // controller:volumetric-measurement:bluetooth:change event. During the 1.5s
    // BLUETOOTH_GRACE_PERIOD_MS the active source can momentarily switch to
    // flow-estimation, so the last event delivered to this plugin before brew:end may
    // have been a 0/stale value even though the scale was healthy and showing weight.
    // That left the window unopened, isExtendedRecording() immediately false, and the
    // DefaultUI auto-steam gate releasing on the next tick — cutting the final drips
    // (PRO-232 repro on 2.0.14).
    //
    // isBluetoothScaleHealthy() is true only when a BLE measurement arrived within
    // BLUETOOTH_GRACE_PERIOD_MS, so this opens the window exactly when there is a genuine
    // BLE scale to settle. On non-NIGHTLY builds isVolumetricAvailable() == this, so the
    // flow-estimation / time-based path keeps isBluetoothScaleHealthy() false and steam
    // still engages immediately (no spurious settle delay). Opening with weight==0 is safe:
    // the settle loop in record() self-terminates via weight stabilization and is hard-
    // capped by EXTENDED_RECORDING_DURATION; it also closes immediately if the scale
    // goes unhealthy (canProcessWeight check there).
    if (shouldOpenExtendedRecording(recording, allowExtendedRecording, controller && controller->isBluetoothScaleHealthy())) {
        // Brew keeps recording briefly so Bluetooth-scale weight can settle.
        extendedRecording = true;
        extendedRecordingStart = millis();
        lastStableWeight = currentBluetoothWeight;
        lastWeightChangeTime = 0;
    }

    recording = false;
    activeShotId.store(0, std::memory_order_relaxed);
    shotGeneration.fetch_add(1, std::memory_order_relaxed);

    xSemaphoreGive(stateMutex);
}

void ShotHistoryPlugin::endExtendedRecording() {
    // Acquire mutex to protect shared state
    if (stateMutex == nullptr || xSemaphoreTake(stateMutex, pdMS_TO_TICKS(STATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW("ShotHistoryPlugin", "Failed to acquire mutex for endExtendedRecording");
        return;
    }

    if (extendedRecording) {
        extendedRecording = false;
    }

    xSemaphoreGive(stateMutex);
}

void ShotHistoryPlugin::recordPhaseTransition(uint8_t phaseNumber, uint16_t sampleIndex) {
    // NOTE: This method must be called while holding stateMutex as it accesses shared state (header, controller)
    // Only record if we have space and a valid header
    if (!controller || header.phaseTransitionCount >= MAX_PHASE_TRANSITIONS || !isFileOpen) {
        return;
    }

    // Get current profile to extract phase name
    Profile profile = controller->getProfileManager()->getSelectedProfile();

    // Validate phaseNumber bounds before accessing profile data
    if (phaseNumber >= profile.phases.size() || phaseNumber >= 255) {
        // Use fallback for out-of-bounds phase numbers
        PhaseTransition &transition = header.phaseTransitions[header.phaseTransitionCount];
        transition.sampleIndex = sampleIndex;
        transition.phaseNumber = phaseNumber;
        transition.reserved = 0;
        snprintf(transition.phaseName, sizeof(transition.phaseName), "Phase %d", phaseNumber + 1);
        header.phaseTransitionCount++;
        ESP_LOGD("ShotHistoryPlugin", "Recorded phase transition to phase %d (fallback name) at sample %d", phaseNumber,
                 sampleIndex);
        return;
    }

    // phaseNumber is now validated, safe to access profile.phases[phaseNumber]
    PhaseTransition &transition = header.phaseTransitions[header.phaseTransitionCount];
    transition.sampleIndex = sampleIndex;
    transition.phaseNumber = phaseNumber;
    transition.reserved = 0;

    strncpy(transition.phaseName, profile.phases[phaseNumber].name.c_str(), sizeof(transition.phaseName) - 1);
    transition.phaseName[sizeof(transition.phaseName) - 1] = '\0';

    header.phaseTransitionCount++;

    ESP_LOGD("ShotHistoryPlugin", "Recorded phase transition to phase %d (%s) at sample %d", phaseNumber, transition.phaseName,
             sampleIndex);
}

uint16_t ShotHistoryPlugin::getSystemInfo() {
    uint16_t systemInfo = 0;

    if (!controller) {
        return systemInfo;
    }

    // Bit 0: Shot started in volumetric mode
    if (shotStartedVolumetric) {
        systemInfo |= SYSTEM_INFO_SHOT_STARTED_VOLUMETRIC;
    }

    // Bit 1: Currently in volumetric mode - use thread-safe method
    if (controller->isBrewProcessVolumetric()) {
        systemInfo |= SYSTEM_INFO_CURRENTLY_VOLUMETRIC;
    }

    // Bit 2: Bluetooth scale connected
    if (controller->isBluetoothScaleHealthy()) {
        systemInfo |= SYSTEM_INFO_BLUETOOTH_SCALE_CONNECTED;
    }

    // Bit 3: Volumetric available
    if (controller->isVolumetricAvailable()) {
        systemInfo |= SYSTEM_INFO_VOLUMETRIC_AVAILABLE;
    }

    // Bit 4: Extended recording active
    if (extendedRecording) {
        systemInfo |= SYSTEM_INFO_EXTENDED_RECORDING;
    }

    return systemInfo;
}

void ShotHistoryPlugin::cleanupHistory() {
    size_t freeSpace = getFreeSpace();
    if (freeSpace > MIN_FREE_SPACE_BYTES) {
        return; // Enough space, nothing to do
    }

    // Collect and sort .slog files to find the oldest
    File directory = fs->open("/h");
    std::vector<String> slogFiles;
    String filename = directory.getNextFileName();
    while (filename != "") {
        if (filename.endsWith(".slog")) {
            slogFiles.push_back(filename);
        }
        filename = directory.getNextFileName();
    }
    directory.close();

    if (slogFiles.empty()) {
        return;
    }

    sort(slogFiles.begin(), slogFiles.end(), [](const String &a, const String &b) { return a < b; });

    // Remove oldest files one at a time until we have enough free space
    size_t removed = 0;
    for (size_t i = 0; i < slogFiles.size() && getFreeSpace() <= MIN_FREE_SPACE_BYTES; i++) {
        String fname = slogFiles[i];
        int start = fname.lastIndexOf('/') + 1;
        int end = fname.lastIndexOf('.');
        uint32_t shotId = 0;
        if (end > start) {
            shotId = fname.substring(start, end).toInt();
        }

        // Serialize both files with every notes reader/writer. The .slog check
        // and sidecar write are one notes resource transaction, so an admitted
        // writer cannot recreate an orphan after retention deletes this shot.
        // Do this before index work: notes saves update index metadata only after
        // releasing notesMutex, preserving the notes-then-index lock order.
        removeHistoryFiles(fname.substring(0, fname.lastIndexOf('.')));
        if (shotId != 0) {
            markIndexDeleted(shotId);
        }
        removed++;
    }

    if (removed > 0) {
        ESP_LOGI("ShotHistoryPlugin", "Cleaned up %u old shots (free space: %u bytes)", removed, getFreeSpace());
    }
}

size_t ShotHistoryPlugin::getFreeSpace() {
    if (!controller) {
        return 0;
    }

    if (controller->isSDCard()) {
        uint64_t total = SD_MMC.totalBytes();
        uint64_t used = SD_MMC.usedBytes();
        uint64_t free = total > used ? (total - used) : 0;
        // Cap to size_t max for consistency
        return free > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(free);
    }
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    return total > used ? (total - used) : 0;
}

void ShotHistoryPlugin::handleRequest(JsonDocument &request, JsonDocument &response) {
    String type = request["tp"].as<String>();
    response["tp"] = String("res:") + type.substring(4);
    response["rid"] = request["rid"].as<String>();

    if (type == "req:history:list") {
        JsonArray arr = response["history"].to<JsonArray>();
        File root = fs->open("/h");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file) {
                String fname = String(file.name());
                if (fname.endsWith(".slog")) {
                    // Read header only
                    ShotLogHeader hdr{};
                    size_t bytesRead = file.read(reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr));

                    // Validate read size
                    if (bytesRead != sizeof(hdr)) {
                        ESP_LOGW("ShotHistoryPlugin", "Failed to read header from %s: expected %zu bytes, got %zu", fname.c_str(),
                                 sizeof(hdr), bytesRead);
                        file = root.openNextFile();
                        continue;
                    }

                    // Validate magic number
                    if (hdr.magic != SHOT_LOG_MAGIC) {
                        ESP_LOGW("ShotHistoryPlugin", "Invalid magic number in %s: 0x%08X (expected 0x%08X)", fname.c_str(),
                                 hdr.magic, SHOT_LOG_MAGIC);
                        file = root.openNextFile();
                        continue;
                    }

                    // File is valid, process it
                    float finalWeight = hdr.finalWeight > 0 ? static_cast<float>(hdr.finalWeight) / WEIGHT_SCALE : 0.0f;

                    bool headerIncomplete = hdr.sampleCount == 0;

                    auto o = arr.add<JsonObject>();
                    int start = fname.lastIndexOf('/') + 1;
                    int end = fname.lastIndexOf('.');
                    String id = fname.substring(start, end);
                    o["id"] = id;
                    o["version"] = hdr.version;
                    o["timestamp"] = hdr.startEpoch;
                    o["profile"] = hdr.profileName;
                    o["profileId"] = hdr.profileId;
                    o["samples"] = hdr.sampleCount;
                    o["duration"] = hdr.durationMs;
                    if (finalWeight > 0.0f) {
                        o["volume"] = finalWeight;
                    }
                    if (headerIncomplete) {
                        o["incomplete"] = true; // flag partial shot
                    }
                }
                file = root.openNextFile();
            }
        }
    } else if (type == "req:history:get") {
        // Return error: binary must be fetched via HTTP endpoint
        response["error"] = "use HTTP /api/history?id=<id>";
    } else if (type == "req:history:delete") {
        auto id = request["id"].as<String>();
        String paddedId = padId(id);
        removeHistoryFiles(paddedId);

        // Notes saves update index metadata only after releasing notesMutex, so
        // deleting the files first preserves that resource lock order.
        markIndexDeleted(id.toInt());

        response["msg"] = "Ok";
    } else if (type == "req:history:notes:get") {
        auto id = request["id"].as<String>();
        if (notesMutex == nullptr || xSemaphoreTake(notesMutex, portMAX_DELAY) != pdTRUE) {
            response["error"] = "Notes unavailable";
        } else {
            JsonDocument notes;
            loadNotes(id, notes);
            response["notes"] = notes;
            xSemaphoreGive(notesMutex);
        }
    } else if (type == "req:history:notes:fill-missing") {
        auto id = request["id"].as<String>();
        const uint32_t requestedId = id.toInt();
        const String requestedGrindSetting = request["notes"]["grindSetting"] | "";
        const shot_notes::ActiveShotIdentity admitted{shotGeneration.load(std::memory_order_acquire),
                                                      activeShotId.load(std::memory_order_acquire)};
        if (requestedGrindSetting.length() == 0 || !shot_notes::isActiveFillFor(admitted, admitted, requestedId)) {
            response["saved"] = false;
        } else if (notesMutex == nullptr || xSemaphoreTake(notesMutex, portMAX_DELAY) != pdTRUE) {
            response["error"] = "Notes unavailable";
        } else {
            // Never acquire stateMutex while notesMutex is held. End/start changes
            // the generation first, and this recheck rejects fills that waited
            // behind completion or another notes write.
            const shot_notes::ActiveShotIdentity current{shotGeneration.load(std::memory_order_acquire),
                                                         activeShotId.load(std::memory_order_acquire)};
            if (!shot_notes::isActiveFillFor(admitted, current, requestedId) ||
                !shot_notes::mayAccessExistingHistoryNotes(fs->exists("/h/" + id + ".slog"))) {
                response["saved"] = false;
            } else {
                JsonDocument existingNotes;
                loadNotes(id, existingNotes);
                if (shot_notes::hasValue(existingNotes["grindSetting"])) {
                    response["saved"] = false;
                } else {
                    const shot_notes::ActiveShotIdentity beforeSave{shotGeneration.load(std::memory_order_acquire),
                                                                    activeShotId.load(std::memory_order_acquire)};
                    if (!shot_notes::isActiveFillFor(admitted, beforeSave, requestedId)) {
                        response["saved"] = false;
                    } else {
                        existingNotes["grindSetting"] = requestedGrindSetting;
                        if (!saveNotes(id, existingNotes)) {
                            response["error"] = "Save failed";
                        } else {
                            response["saved"] = true;
                        }
                    }
                }
            }
            xSemaphoreGive(notesMutex);
        }
    } else if (type == "req:history:notes:save") {
        auto id = request["id"].as<String>();
        if (notesMutex == nullptr || xSemaphoreTake(notesMutex, portMAX_DELAY) != pdTRUE) {
            response["error"] = "Notes unavailable";
            return;
        }
        JsonDocument notes; // explicit document: variant->const JsonDocument& is ambiguous on clang
        notes.set(request["notes"]);
        JsonDocument previousNotes;
        const bool historyExists = shot_notes::mayAccessExistingHistoryNotes(fs->exists("/h/" + id + ".slog"));
        if (historyExists) {
            loadNotes(id, previousNotes);
        }
        // A normal editor save can be based on a stale GET. Preserve a Dashboard
        // fill that landed after that read unless this payload explicitly edits it.
        shot_notes::preservePersistedGrindSetting(notes, previousNotes);
        const bool notesSaved = historyExists && saveNotes(id, notes);
        bool beanUsageSaved = false;
        if (!notesSaved) {
            response["error"] = "Save failed";
        } else {
            beanUsageSaved = applyBeanUsageDelta(previousNotes, notes);
        }

        if (notesSaved && !beanUsageSaved) {
            response["error"] = "Bean usage update failed";
        }

        if (notesSaved && beanUsageSaved) {
            // Update rating and volume in index
            uint8_t rating = notes["rating"].as<uint8_t>();

            // Check if user provided a doseOut value to override volume
            uint16_t volume = 0;
            if (notes["doseOut"].is<String>() && !notes["doseOut"].as<String>().isEmpty()) {
                float doseOut = notes["doseOut"].as<String>().toFloat();
                if (doseOut > 0.0f) {
                    volume = encodeUnsigned(doseOut, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
                }
            }

            // Always use updateIndexMetadata - it handles both rating and optional volume
            updateIndexMetadata(id.toInt(), rating, volume);

            response["msg"] = "Ok";
        }
        xSemaphoreGive(notesMutex);
    } else if (type == "req:history:rebuild") {
        // Rebuild is now handled asynchronously by WebUIPlugin
        // This path shouldn't be reached, but handle it just in case
        response["msg"] = "Use async rebuild";
    }
}

bool ShotHistoryPlugin::saveNotes(const String &id, const JsonDocument &notes) {
    File file = fs->open("/h/" + id + ".json", FILE_WRITE);
    if (!file) {
        return false;
    }
    String notesStr;
    serializeJson(notes, notesStr);
    size_t written = file.print(notesStr);
    if (written != notesStr.length()) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to write notes: expected %u, wrote %zu", notesStr.length(), written);
        file.close();
        return false;
    }
    file.close();
    return true;
}

void ShotHistoryPlugin::removeHistoryFiles(const String &id) {
    if (notesMutex == nullptr || xSemaphoreTake(notesMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW("ShotHistoryPlugin", "Failed to acquire notes mutex for history removal");
        return;
    }
    fs->remove("/h/" + id + ".slog");
    fs->remove("/h/" + id + ".json");
    xSemaphoreGive(notesMutex);
}

void ShotHistoryPlugin::loadNotes(const String &id, JsonDocument &notes) {
    File file = fs->open("/h/" + id + ".json", "r");
    if (file) {
        String notesStr = file.readString();
        file.close();
        DeserializationError err = deserializeJson(notes, notesStr);
        if (err != DeserializationError::Ok) {
            ESP_LOGE("ShotHistoryPlugin", "Failed to parse notes JSON for %s: %s", id.c_str(), err.c_str());
        }
    }
}

bool ShotHistoryPlugin::applyBeanUsageDelta(JsonVariantConst previousNotes, JsonVariantConst nextNotes) {
    if (!controller || !controller->getBeanManager()) {
        return true;
    }

    BeanManager *manager = controller->getBeanManager();
    std::vector<BeanEntry> beans = manager->listBeans();
    const float previousDose = parseDoseValue(previousNotes["doseIn"]);
    const float nextDose = parseDoseValue(nextNotes["doseIn"]);
    const int previousBeanIndex = findBeanIndexForNotes(previousNotes, beans);
    const int nextBeanIndex = findBeanIndexForNotes(nextNotes, beans);

    auto adjustBean = [&](int beanIndex, float delta, BeanEntry *originalBean) -> bool {
        if (beanIndex < 0 || beanIndex >= static_cast<int>(beans.size()) || !std::isfinite(delta) || delta == 0.0f) {
            return true;
        }

        BeanEntry bean = beans[beanIndex];
        if (bean.quantity < 0.0f) {
            return true;
        }
        if (originalBean) {
            *originalBean = bean;
        }

        bean.quantity = std::max(0.0f, roundBeanQuantity(bean.quantity + delta));
        return manager->saveBean(bean);
    };

    auto restoreBean = [&](const BeanEntry &bean) -> bool {
        BeanEntry restored = bean;
        return manager->saveBean(restored);
    };

    if (previousBeanIndex >= 0 && nextBeanIndex >= 0 && beans[previousBeanIndex].id == beans[nextBeanIndex].id) {
        return adjustBean(nextBeanIndex, previousDose - nextDose, nullptr);
    }

    BeanEntry previousOriginal;
    const bool previousChanged = previousBeanIndex >= 0 && previousBeanIndex < static_cast<int>(beans.size()) &&
                                 std::isfinite(previousDose) && previousDose != 0.0f && beans[previousBeanIndex].quantity >= 0.0f;
    if (!adjustBean(previousBeanIndex, previousDose, previousChanged ? &previousOriginal : nullptr)) {
        return false;
    }

    if (!adjustBean(nextBeanIndex, -nextDose, nullptr)) {
        if (previousChanged && !restoreBean(previousOriginal)) {
            ESP_LOGE("ShotHistoryPlugin", "Failed to roll back bean quantity update for bean %s", previousOriginal.id.c_str());
        }
        return false;
    }

    return true;
}

void ShotHistoryPlugin::loopTask(void *arg) {
    auto *plugin = static_cast<ShotHistoryPlugin *>(arg);
    while (true) {
        plugin->record();
        // Use canonical interval from shot log format to avoid divergence.
        vTaskDelay(SHOT_LOG_SAMPLE_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

bool ShotHistoryPlugin::flushBuffer() {
    if (isFileOpen && ioBufferPos > 0) {
        size_t written = currentFile.write(ioBuffer, ioBufferPos);
        if (written != ioBufferPos) {
            ESP_LOGE("ShotHistoryPlugin", "Failed to write buffer: expected %zu, wrote %zu", ioBufferPos, written);
            return false;
        }
        ioBufferPos = 0;
    }
    return true;
}

// Index management methods
bool ShotHistoryPlugin::ensureIndexExists() {
    if (fs->exists("/h/index.bin")) {
        // Validate existing index header
        File indexFile = fs->open("/h/index.bin", "r");
        if (indexFile) {
            ShotIndexHeader hdr{};
            bool valid =
                (indexFile.read(reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)) == sizeof(hdr) && hdr.magic == SHOT_INDEX_MAGIC);
            indexFile.close();
            if (valid) {
                return true;
            }
            ESP_LOGW("ShotHistoryPlugin", "Corrupt index file detected (bad magic), recreating");
            fs->remove("/h/index.bin");
        }
    }

    // Create new empty index
    File indexFile = fs->open("/h/index.bin", FILE_WRITE);
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create index file");
        return false;
    }

    ShotIndexHeader header{};
    header.magic = SHOT_INDEX_MAGIC;
    header.version = SHOT_INDEX_VERSION;
    header.entrySize = SHOT_INDEX_ENTRY_SIZE;
    header.entryCount = 0;
    header.nextId = controller->getSettings().getHistoryIndex();

    indexFile.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
    indexFile.close();

    ESP_LOGI("ShotHistoryPlugin", "Created new index file");
    return true;
}

bool ShotHistoryPlugin::appendToIndex(const ShotIndexEntry &entry) {
    IndexLockGuard guard(indexMutex);
    return appendToIndexLocked(entry);
}

bool ShotHistoryPlugin::appendToIndexLocked(const ShotIndexEntry &entry) {
    if (!ensureIndexExists()) {
        return false;
    }

    File indexFile = fs->open("/h/index.bin", "r+");
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to open index file for append");
        return false;
    }

    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        indexFile.close();
        return false;
    }

    // Check for existing entry with same ID - update in place (upsert)
    int existingPos = findEntryPosition(indexFile, header, entry.id);
    if (existingPos >= 0) {
        if (writeEntryAtPosition(indexFile, existingPos, entry)) {
            ESP_LOGD("ShotHistoryPlugin", "Updated existing index entry for shot %u", entry.id);
            indexFile.close();
            return true;
        }
        ESP_LOGE("ShotHistoryPlugin", "Failed to update existing index entry for shot %u", entry.id);
        indexFile.close();
        return false;
    }

    // Append entry
    indexFile.seek(0, SeekEnd);
    size_t written = indexFile.write(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry));
    if (written != sizeof(entry)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to write index entry for shot %u", entry.id);
        indexFile.close();
        return false;
    }

    // Update header
    header.entryCount++;
    header.nextId = entry.id + 1;
    indexFile.seek(0, SeekSet);
    indexFile.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));

    indexFile.close();
    ESP_LOGD("ShotHistoryPlugin", "Appended shot %u to index", entry.id);
    return true;
}

void ShotHistoryPlugin::updateIndexMetadata(uint32_t shotId, uint8_t rating, uint16_t volume) {
    IndexLockGuard guard(indexMutex);
    updateIndexMetadataLocked(shotId, rating, volume);
}

void ShotHistoryPlugin::updateIndexMetadataLocked(uint32_t shotId, uint8_t rating, uint16_t volume) {
    File indexFile = fs->open("/h/index.bin", "r+");
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to open index file for metadata update");
        return;
    }

    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        indexFile.close();
        return;
    }

    int entryPos = findEntryPosition(indexFile, header, shotId);
    if (entryPos >= 0) {
        ShotIndexEntry entry{};
        if (readEntryAtPosition(indexFile, entryPos, entry)) {
            // PRO-277: apply the pure merge rule (rating always, volume only on a
            // positive override, HAS_NOTES set when rated) — see ShotIndexMetadataPolicy.h.
            entry = applyIndexMetadata(entry, rating, volume);

            if (writeEntryAtPosition(indexFile, entryPos, entry)) {
                ESP_LOGD("ShotHistoryPlugin", "Updated metadata for shot %u: rating=%u, volume=%u", shotId, rating, volume);
            }
        }
    } else {
        ESP_LOGW("ShotHistoryPlugin", "Shot %u not found in index for metadata update", shotId);
    }

    indexFile.close();
}

void ShotHistoryPlugin::markIndexDeleted(uint32_t shotId) {
    IndexLockGuard guard(indexMutex);
    markIndexDeletedLocked(shotId);
}

void ShotHistoryPlugin::markIndexDeletedLocked(uint32_t shotId) {
    File indexFile = fs->open("/h/index.bin", "r+");
    if (!indexFile) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to open index file for deletion marking");
        return;
    }

    ShotIndexHeader header{};
    if (!readIndexHeader(indexFile, header)) {
        indexFile.close();
        return;
    }

    // Find ALL entries with this shot ID and mark them as deleted
    uint32_t duplicatesFound = 0;

    for (uint32_t i = 0; i < header.entryCount; i++) {
        size_t entryPos = sizeof(ShotIndexHeader) + i * sizeof(ShotIndexEntry);
        ShotIndexEntry entry{};
        if (readEntryAtPosition(indexFile, entryPos, entry)) {
            if (entry.id == shotId) {
                duplicatesFound++;

                // Mark this entry as deleted
                entry.flags |= SHOT_FLAG_DELETED;

                if (writeEntryAtPosition(indexFile, entryPos, entry)) {
                    ESP_LOGD("ShotHistoryPlugin", "Marked shot %u as deleted in index (duplicate #%u)", shotId, duplicatesFound);
                }
            }
        }
    }

    if (duplicatesFound == 0) {
        ESP_LOGW("ShotHistoryPlugin", "Shot %u not found in index for deletion marking", shotId);
    } else if (duplicatesFound > 1) {
        ESP_LOGW("ShotHistoryPlugin", "Found and marked %u duplicate entries for shot %u as deleted", duplicatesFound, shotId);
    }

    indexFile.close();
}

void ShotHistoryPlugin::startAsyncRebuild() {
    if (!rebuildInProgress) {
        rebuildInProgress = true; // Set immediately to prevent multiple rebuilds
        ESP_LOGI("ShotHistoryPlugin", "Starting immediate async rebuild task");

        // Create a dedicated task for rebuild instead of using the existing loop
        xTaskCreatePinnedToCore(
            [](void *param) {
                auto *plugin = static_cast<ShotHistoryPlugin *>(param);
                ESP_LOGI("ShotHistoryPlugin", "Rebuild task started");
                plugin->rebuildIndex();
                plugin->rebuildInProgress = false;
                ESP_LOGI("ShotHistoryPlugin", "Rebuild task completed");
                vTaskDelete(NULL); // Delete this task when done
            },
            "ShotHistoryRebuild",
            configMINIMAL_STACK_SIZE * 8, // Larger stack for file operations
            this,
            2, // Higher priority than normal
            NULL, 0);
    } else {
        ESP_LOGW("ShotHistoryPlugin", "Rebuild already in progress, ignoring request");
    }
}

void ShotHistoryPlugin::rebuildIndex() {
    IndexLockGuard guard(indexMutex);
    rebuildIndexLocked();
}

void ShotHistoryPlugin::rebuildIndexLocked() {
    ESP_LOGI("ShotHistoryPlugin", "Starting index rebuild...");

    // Send scanning event
    if (pluginManager) {
        Event startEvent;
        startEvent.id = EventIds::EVT_HISTORY_REBUILD_PROGRESS;
        startEvent.setInt("total", 0);
        startEvent.setInt("current", 0);
        startEvent.setString("status", "scanning");
        pluginManager->trigger(startEvent);
    }

    // Delete existing index
    fs->remove("/h/index.bin");

    // Create new empty index
    if (!ensureIndexExists()) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create index during rebuild");
        // Emit error event
        if (pluginManager) {
            Event errorEvent;
            errorEvent.id = EventIds::EVT_HISTORY_REBUILD_PROGRESS;
            errorEvent.setInt("total", 0);
            errorEvent.setInt("current", 0);
            errorEvent.setString("status", "error");
            pluginManager->trigger(errorEvent);
        }
        return;
    }

    File directory = fs->open("/h");
    if (!directory || !directory.isDirectory()) {
        ESP_LOGW("ShotHistoryPlugin", "No history directory found");
        // Emit completion event even if no directory exists
        if (pluginManager) {
            Event completedEvent;
            completedEvent.id = EventIds::EVT_HISTORY_REBUILD_PROGRESS;
            completedEvent.setInt("total", 0);
            completedEvent.setInt("current", 0);
            completedEvent.setString("status", "completed");
            pluginManager->trigger(completedEvent);
        }
        return;
    }

    // Collect all .slog files
    std::vector<String> slogFiles;
    File file = directory.openNextFile();
    while (file) {
        String fname = String(file.name());
        if (fname.endsWith(".slog")) {
            slogFiles.push_back(fname);
        }
        file = directory.openNextFile();
    }
    directory.close();

    // Sort files to maintain order
    std::sort(slogFiles.begin(), slogFiles.end());

    ESP_LOGI("ShotHistoryPlugin", "Rebuilding index from %d shot files", slogFiles.size());

    // Emit start event with total file count
    if (pluginManager) {
        Event startEvent;
        startEvent.id = EventIds::EVT_HISTORY_REBUILD_PROGRESS;
        startEvent.setInt("total", (int)slogFiles.size());
        startEvent.setInt("current", 0);
        startEvent.setString("status", "started");
        pluginManager->trigger(startEvent);
    }

    int currentIndex = 0;
    for (const String &fileName : slogFiles) {
        currentIndex++;
        File shotFile = fs->open("/h/" + fileName, "r");
        if (!shotFile) {
            continue;
        }

        // Read shot header
        ShotLogHeader shotHeader{};
        if (shotFile.read(reinterpret_cast<uint8_t *>(&shotHeader), sizeof(shotHeader)) != sizeof(shotHeader) ||
            shotHeader.magic != SHOT_LOG_MAGIC) {
            shotFile.close();
            continue;
        }

        // Extract shot ID from filename
        int start = fileName.lastIndexOf('/') + 1;
        int end = fileName.lastIndexOf('.');
        uint32_t shotId = fileName.substring(start, end).toInt();

        // Create index entry
        ShotIndexEntry entry{};
        entry.id = shotId;
        entry.timestamp = shotHeader.startEpoch;
        entry.duration = shotHeader.durationMs;
        entry.volume = shotHeader.finalWeight;
        entry.rating = 0; // Will be updated if notes exist
        entry.flags = SHOT_FLAG_COMPLETED;
        strncpy(entry.profileId, shotHeader.profileId, sizeof(entry.profileId) - 1);
        entry.profileId[sizeof(entry.profileId) - 1] = '\0';
        strncpy(entry.profileName, shotHeader.profileName, sizeof(entry.profileName) - 1);
        entry.profileName[sizeof(entry.profileName) - 1] = '\0';

        // Check for incomplete shots
        if (shotHeader.sampleCount == 0) {
            entry.flags &= ~SHOT_FLAG_COMPLETED;
        }

        // Check for notes and extract rating and volume override
        // Use padId to match the format used during normal recording
        String notesPath = "/h/" + padId(String(shotId, 10)) + ".json";
        if (fs->exists(notesPath)) {
            entry.flags |= SHOT_FLAG_HAS_NOTES;

            File notesFile = fs->open(notesPath, "r");
            if (notesFile) {
                String notesStr = notesFile.readString();
                notesFile.close();

                JsonDocument notesDoc;
                if (deserializeJson(notesDoc, notesStr) == DeserializationError::Ok) {
                    entry.rating = notesDoc["rating"].as<uint8_t>();

                    // Check if user provided a doseOut value to override volume
                    if (notesDoc["doseOut"].is<String>() && !notesDoc["doseOut"].as<String>().isEmpty()) {
                        float doseOut = notesDoc["doseOut"].as<String>().toFloat();
                        if (doseOut > 0.0f) {
                            entry.volume = encodeUnsigned(doseOut, WEIGHT_SCALE, WEIGHT_MAX_VALUE);
                        }
                    }
                }
            }
        }

        shotFile.close();

        // Append to index. We already hold indexMutex (rebuildIndex took it), so
        // call the unlocked variant — appendToIndex() would re-take the non-recursive
        // mutex and self-deadlock. PRO-277.
        appendToIndexLocked(entry);

        // Emit progress update with adaptive frequency
        // Update every file for small rebuilds, every few files for larger ones
        int updateFrequency = slogFiles.size() <= 20 ? 1 : (slogFiles.size() <= 100 ? 3 : 5);
        if (pluginManager && (currentIndex % updateFrequency == 0 || currentIndex == slogFiles.size())) {
            Event progressEvent;
            progressEvent.id = EventIds::EVT_HISTORY_REBUILD_PROGRESS;
            progressEvent.setInt("total", (int)slogFiles.size());
            progressEvent.setInt("current", currentIndex);
            progressEvent.setString("status", "processing");
            pluginManager->trigger(progressEvent);
            ESP_LOGI("ShotHistoryPlugin", "Rebuild progress: %d/%d", currentIndex, (int)slogFiles.size());

            // Small delay to allow UI updates and prevent overwhelming the system
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // Emit completion event
    if (pluginManager) {
        Event completionEvent;
        completionEvent.id = EventIds::EVT_HISTORY_REBUILD_PROGRESS;
        completionEvent.setInt("total", (int)slogFiles.size());
        completionEvent.setInt("current", (int)slogFiles.size());
        completionEvent.setString("status", "completed");
        pluginManager->trigger(completionEvent);
    }

    ESP_LOGI("ShotHistoryPlugin", "Index rebuild completed");
}

// Index helper functions
bool ShotHistoryPlugin::readIndexHeader(File &indexFile, ShotIndexHeader &header) {
    if (indexFile.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to read index header");
        return false;
    }
    if (header.magic != SHOT_INDEX_MAGIC) {
        ESP_LOGE("ShotHistoryPlugin", "Invalid index magic: 0x%08X", header.magic);
        return false;
    }
    return true;
}

int ShotHistoryPlugin::findEntryPosition(File &indexFile, const ShotIndexHeader &header, uint32_t shotId) {
    for (uint32_t i = 0; i < header.entryCount; i++) {
        size_t entryPos = sizeof(ShotIndexHeader) + i * sizeof(ShotIndexEntry);
        indexFile.seek(entryPos, SeekSet);

        ShotIndexEntry entry{};
        if (!readEntryAtPosition(indexFile, entryPos, entry)) {
            ESP_LOGW("ShotHistoryPlugin", "Failed to read entry at position %u", i);
            break;
        }

        if (entry.id == shotId) {
            return entryPos;
        }
    }
    return -1;
}

bool ShotHistoryPlugin::readEntryAtPosition(File &indexFile, size_t position, ShotIndexEntry &entry) {
    indexFile.seek(position, SeekSet);
    if (indexFile.read(reinterpret_cast<uint8_t *>(&entry), sizeof(entry)) != sizeof(entry)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to read entry at position %zu", position);
        return false;
    }
    return true;
}

bool ShotHistoryPlugin::writeEntryAtPosition(File &indexFile, size_t position, const ShotIndexEntry &entry) {
    indexFile.seek(position, SeekSet);
    if (indexFile.write(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry)) != sizeof(entry)) {
        ESP_LOGE("ShotHistoryPlugin", "Failed to write entry at position %zu", position);
        return false;
    }
    return true;
}

bool ShotHistoryPlugin::createEarlyIndexEntry() {
    ShotIndexEntry indexEntry{};
    indexEntry.id = currentId.toInt();
    indexEntry.timestamp = header.startEpoch;
    indexEntry.duration = 0; // Will be overwritten on completion
    indexEntry.volume = 0;   // Will be overwritten on completion
    indexEntry.rating = 0;
    indexEntry.flags = 0; // No SHOT_FLAG_COMPLETED - indicates in-progress shot
    if (controller->getMode() == MODE_MANUAL || controller->getProcessType() == MODE_MANUAL) {
        strncpy(indexEntry.profileId, "manual", sizeof(indexEntry.profileId) - 1);
        indexEntry.profileId[sizeof(indexEntry.profileId) - 1] = '\0';
        strncpy(indexEntry.profileName, "Manual", sizeof(indexEntry.profileName) - 1);
        indexEntry.profileName[sizeof(indexEntry.profileName) - 1] = '\0';
    } else {
        Profile profile = controller->getProfileManager()->getSelectedProfile();
        strncpy(indexEntry.profileId, profile.id.c_str(), sizeof(indexEntry.profileId) - 1);
        indexEntry.profileId[sizeof(indexEntry.profileId) - 1] = '\0';
        strncpy(indexEntry.profileName, profile.label.c_str(), sizeof(indexEntry.profileName) - 1);
        indexEntry.profileName[sizeof(indexEntry.profileName) - 1] = '\0';
    }

    bool success = appendToIndex(indexEntry);
    if (success) {
        ESP_LOGD("ShotHistoryPlugin", "Created early index entry for shot %u", indexEntry.id);
    } else {
        ESP_LOGE("ShotHistoryPlugin", "Failed to create early index entry for shot %u", indexEntry.id);
    }
    return success;
}
