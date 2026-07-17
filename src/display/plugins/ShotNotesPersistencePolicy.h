#pragma once

#include <ArduinoJson.h>

namespace shot_notes {

inline bool hasValue(JsonVariantConst value) { return value.is<const char *>() && *value.as<const char *>() != '\0'; }

// Every notes reader/writer runs under notesMutex. Once a history delete removes
// the .slog record, queued notes work must not recreate its .json sidecar.
inline bool mayAccessExistingHistoryNotes(bool historyExists) { return historyExists; }

// A normal Shot History save carries the complete editor form and may therefore
// contain a blank grindSetting. Preserve a non-blank value that was persisted
// after that form was read (for example by Dashboard's atomic fill request).
// A non-blank editor value is an explicit Shot History edit and remains
// authoritative.
inline void preservePersistedGrindSetting(JsonDocument &incoming, JsonVariantConst persisted) {
    if (!hasValue(incoming["grindSetting"]) && hasValue(persisted["grindSetting"])) {
        incoming["grindSetting"] = persisted["grindSetting"];
    }
}

} // namespace shot_notes
