#ifndef SLOG_TO_PROFILE_CONVERTER_H
#define SLOG_TO_PROFILE_CONVERTER_H

#include <display/models/profile.h>
#include <display/models/shot_log_format.h>
#include <FS.h>

class SlogToProfileConverter {
  public:
    static Profile convert(const String &slogPath, const String &label, FS *fs);
};

#endif // SLOG_TO_PROFILE_CONVERTER_H
