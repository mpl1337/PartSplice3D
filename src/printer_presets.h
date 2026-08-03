#pragma once

#include <array>

struct BambuPrinterPreset {
    const char* label;
    const char* printerSettingsId;
    const char* printSettingsId;
    const char* filamentSettingsId;
    double bedWidth;
    double bedDepth;
};

extern const std::array<BambuPrinterPreset, 14> kBambuPrinterPresets;
