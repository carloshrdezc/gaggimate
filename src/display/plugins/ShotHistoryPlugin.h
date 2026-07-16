#ifndef SHOTHISTORYPLUGIN_H
#define SHOTHISTORYPLUGIN_H

#include "PostStopGracePolicy.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <atomic>
#include <display/core/Plugin.h>
#include <display/core/VolumetricCoalescer.h>
#include <display/core/utils.h>
#include <display/models/shot_log_format.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

constexpr size_t SHOT_HISTORY_INTERVAL = 100;
constexpr size_t MIN_FREE_SPACE_BYTES = 500 * 1024; // 500 KB reserved free space
// PRO-248: hard cap for the post-stop extended-recording / weight-settle window.
// Defined as POST_STOP_GRACE_DURATION_MS (single source of truth) so this window
// and BLEScalePlugin's scale-alive grace cap share one constant. The meaningful
// value-pin (POST_STOP_GRACE_DURATION_MS == 10000) lives in the host tests.
constexpr unsigned long EXTENDED_RECORDING_DURATION = POST_STOP_GRACE_DURATION_MS;
constexpr unsigned long WEIGHT_STABILIZATION_TIME = 1000; // 1 second
constexpr float WEIGHT_STABILIZATION_THRESHOLD = 0.1f;    // 0.1g threshold
constexpr int SHOT_ID_LENGTH = 6;                         // Shot ID padding length
constexpr unsigned long STATE_MUTEX_TIMEOUT_MS = 100;     // Mutex timeout for state access

class ShotHistoryPlugin : public Plugin {
  public:
    ShotHistoryPlugin() = default;

    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override {};

    void record();

    void handleRequest(JsonDocument &request, JsonDocument &response);

    // Index management methods
    bool appendToIndex(const ShotIndexEntry &entry);
    void updateIndexMetadata(uint32_t shotId, uint8_t rating, uint16_t volume);
    void markIndexDeleted(uint32_t shotId);
    void rebuildIndex();
    void startAsyncRebuild();
    bool ensureIndexExists();

    // Get current shot ID for WebUIPlugin status updates
    String getCurrentShotId() const { return currentId; }
    // Check if a shot is currently being recorded
    bool isRecording() const { return recording.load(std::memory_order_relaxed); }
    // Check if the post-stop extended-recording / weight-settle window is active.
    // Used by the UI to hold the auto-steam transition until the final shot yield
    // has been captured (PRO-223). Returns false when no settle window is running
    // (e.g. no BLE scale), so callers must not block indefinitely on it.
    bool isExtendedRecording() const { return extendedRecording.load(std::memory_order_relaxed); }

  private:
    // Index helper functions
    // PRO-277: all four /h/index.bin entry points (appendToIndex, updateIndexMetadata,
    // markIndexDeleted, rebuildIndex) are reached from MULTIPLE tasks — the ShotHistory
    // loopTask (record() -> createEarlyIndexEntry / appendCompletedShotToIndex), the
    // WebUI request task (handleRequest -> updateIndexMetadata / markIndexDeleted), and
    // the dedicated async-rebuild task. They each did their own fs->open(.."r+") with no
    // serialization, so concurrent opens of the same file corrupted/lost writes and the
    // post-shot metadata update could not find a freshly-appended entry. indexMutex
    // serializes the whole body of each; the public methods take it and delegate to the
    // *Locked() implementations below. These never call each other, so a non-recursive
    // mutex is deadlock-free.
    bool appendToIndexLocked(const ShotIndexEntry &entry);
    void updateIndexMetadataLocked(uint32_t shotId, uint8_t rating, uint16_t volume);
    void markIndexDeletedLocked(uint32_t shotId);
    void rebuildIndexLocked();
    bool readIndexHeader(File &indexFile, ShotIndexHeader &header);
    int findEntryPosition(File &indexFile, const ShotIndexHeader &header, uint32_t shotId);
    bool readEntryAtPosition(File &indexFile, size_t position, ShotIndexEntry &entry);
    bool writeEntryAtPosition(File &indexFile, size_t position, const ShotIndexEntry &entry);
    bool createEarlyIndexEntry();
    bool saveNotes(const String &id, const JsonDocument &notes);
    void loadNotes(const String &id, JsonDocument &notes);
    void removeHistoryFiles(const String &id);
    bool applyBeanUsageDelta(JsonVariantConst previousNotes, JsonVariantConst nextNotes);
    void startRecording();

    uint16_t getSystemInfo(); // Helper to pack system state bits

    // Phase 1 refactoring: extracted helper methods from record()
    bool openLogFileIfNeeded();
    void initializeHeader();
    // PRO-277: createSample / updateBluetoothFlow take the telemetry snapshot
    // captured under stateMutex in record(), so the file-building work runs
    // lock-free and can no longer block the event callbacks.
    ShotLogSample createSample(float bluetoothWeight, float estimatedWeight, float temperature, float puckResistance);
    void updateBluetoothFlow(float bluetoothWeight);
    bool writeSampleToBuffer(const ShotLogSample &sample);
    void checkEarlyIndexCreation();
    bool closeLogFile(float finalBluetoothWeight);
    void patchHeaderWithFinalData(float finalBluetoothWeight);
    bool isShotTooShort() const;
    void handleFailedShot(const String &id, bool hadIndexEntry);
    void handleCompletedShot(const String &id, const String &beanName, const String &beanId, const String &grinderName,
                             bool startedVolumetric, double grindTargetVolume, int grindTargetDuration,
                             const ShotLogHeader &completedHeader);
    void appendCompletedShotToIndex(const String &id, const ShotLogHeader &completedHeader, bool hasNotes = false);

    // PRO-422: resolve the selected bean NAME (all Settings stores) to its stable
    // BeanManager id at capture time. Returns "" when no bean is selected, the
    // BeanManager is unavailable, or no stored bean matches the name.
    String resolveSelectedBeanId(const String &beanName);

    unsigned long getTime();

    void endRecording(bool allowExtendedRecording = true);
    void endExtendedRecording();
    void cleanupHistory();
    size_t getFreeSpace();

    void recordPhaseTransition(uint8_t phaseNumber, uint16_t sampleIndex); // Helper for phase transitions

    Controller *controller = nullptr;
    PluginManager *pluginManager = nullptr;
    FS *fs = &LittleFS;
    String currentId = "";
    bool isFileOpen = false;
    File currentFile;
    ShotLogHeader header{};
    uint32_t sampleCount = 0;
    uint8_t ioBuffer[4096];
    size_t ioBufferPos = 0; // bytes used

    // Set from the controller task (core 1, via startRecording()/endRecording() under stateMutex) and
    // cleared from the ShotHistory loopTask (core 0, in record()). Read lock-free cross-task via
    // isRecording()/isExtendedRecording(); std::atomic gives a correct, self-documenting cross-core flag
    // (relaxed ordering — a single independent bool, no dependent data published alongside it).
    std::atomic<bool> recording{false};
    std::atomic<bool> extendedRecording{false};
    std::atomic<uint32_t> activeShotId{0};
    std::atomic<uint32_t> shotGeneration{0};
    bool indexEntryCreated = false;     // Track if early index entry was created
    bool shotStartedVolumetric = false; // Track initial volumetric mode
    unsigned long shotStart = 0;
    unsigned long extendedRecordingStart = 0;
    unsigned long lastWeightChangeTime = 0;
    float currentTemperature = 0.0f;
    float currentBluetoothWeight = 0.0f;
    // PRO-367: coalesce-latest for the bluetooth-weight recording handler. The
    // handler takes stateMutex with a fail-fast timeout; on a failed take the old
    // code DROPPED the weight, so the RECORDED yield could lag the settled weight.
    // The scale weight is monotonic cumulative, so we latch the freshest value on
    // a failed take and apply it on the next successful take instead of dropping.
    volumetric::Coalescer bluetoothWeightCoalescer{};
    float lastStableWeight = 0.0f;
    float lastBluetoothWeight = 0.0f;
    float currentBluetoothFlow = 0.0f;
    float currentEstimatedWeight = 0.0f;
    float currentPuckResistance = 0.0f;
    String currentProfileName;
    String currentBeanName;
    String currentBeanId;      // PRO-422: BeanManager id of the selected bean at brew start ("" if none/unknown)
    String currentGrinderName; // PRO-428: selected grinder name at brew start ("" if none); stamped as notes "grinderName"
    // PRO-441: snapshot the MACHINE GRIND TARGET (auto-grind grams/seconds) at brew start, mirroring the
    // grinderName capture. Stamped as notes "grindTarget" (a display label string) so every web client reads
    // the same device-authoritative target instead of a per-browser localStorage selection log. tgv/tgd are
    // already Settings/NVS-persisted — no new NVS key, no new WebSocket message. The volumetric-vs-time mode
    // reuses shotStartedVolumetric (same isVolumetricTarget() snapshot) rather than a second copy.
    double currentGrindTargetVolume = 0; // grams
    int currentGrindTargetDuration = 0;  // milliseconds

    // Phase transition tracking (v5+)
    uint8_t lastRecordedPhase = 0xFF; // Invalid initial value to detect first phase

    // Async rebuild state
    bool rebuildInProgress = false;

    xTaskHandle taskHandle;
    SemaphoreHandle_t stateMutex = nullptr;     // Protects shared state accessed by record()
    SemaphoreHandle_t recordingMutex = nullptr; // Prevents a start while an ended file is closing.
    // Serializes notes filesystem read/merge/write operations without blocking
    // telemetry callbacks on stateMutex.
    SemaphoreHandle_t notesMutex = nullptr;
    // PRO-277: serializes every operation on /h/index.bin across the loopTask,
    // the WebUI request task, and the async-rebuild task (see *Locked helpers above).
    SemaphoreHandle_t indexMutex = nullptr;
    bool flushBuffer();
    static void loopTask(void *arg);
};

extern ShotHistoryPlugin ShotHistory;

#endif // SHOTHISTORYPLUGIN_H
