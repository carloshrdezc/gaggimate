#ifndef BLESCALESCANPOLICY_H
#define BLESCALESCANPOLICY_H

#include <display/core/constants.h>

constexpr bool shouldScanForBleScaleMode(int mode) {
    return mode == MODE_BREW || mode == MODE_GRIND || mode == MODE_MANUAL;
}

#endif // BLESCALESCANPOLICY_H
