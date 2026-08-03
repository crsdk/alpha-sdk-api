// RestPropertyParsers — see RestPropertyParsers.h. MIT-licensed REST helpers
// ported from the REST server's original additions to the SDK sample.

// On Windows, process <windows.h> before Text.h (pulled by the header below).
// Text.h redefines TCHAR to char, which corrupts <winnt.h> if it is included
// afterwards. See CameraWebController.h for details.
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "RestPropertyParsers.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "CameraRemote_SDK.h"

namespace SDK = SCRSDK;

namespace cli {

// --- Buffer parser -------------------------------------------------------
std::vector<std::uint16_t> parse_still_image_store_destination(unsigned char const* buf,
                                                               std::uint32_t nval) {
    std::vector<std::uint16_t> result(nval);
    std::memcpy(result.data(), buf, nval * sizeof(std::uint16_t));
    return result;
}

namespace {
std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return r;
}

std::string normalize_string(const std::string& str) {
    std::string result = to_lower(str);
    std::replace(result.begin(), result.end(), '_', '-');
    std::replace(result.begin(), result.end(), ' ', '-');
    return result;
}
}  // namespace

// --- Formatters ----------------------------------------------------------
text format_file_type(std::uint16_t file_type) {
    text_stringstream ts;
    switch (file_type) {
    case SDK::CrFileType_None:    ts << "None"; break;
    case SDK::CrFileType_Jpeg:    ts << "JPEG"; break;
    case SDK::CrFileType_Raw:     ts << "RAW"; break;
    case SDK::CrFileType_RawJpeg: ts << "RAW+JPEG"; break;
    case SDK::CrFileType_RawHeif: ts << "RAW+HEIF"; break;
    case SDK::CrFileType_Heif:    ts << "HEIF"; break;
    default:                      ts << "Unknown(" << file_type << ")"; break;
    }
    return ts.str();
}

text format_still_image_quality(std::uint32_t image_quality) {
    text_stringstream ts;
    // Simple quality value (no file-type bits).
    if (image_quality < 256) {
        switch (image_quality) {
        case SDK::CrImageQuality_Light:    ts << "Light"; break;
        case SDK::CrImageQuality_Standard: ts << "Standard"; break;
        case SDK::CrImageQuality_Fine:     ts << "Fine"; break;
        case SDK::CrImageQuality_ExFine:   ts << "Extra Fine"; break;
        default:                           ts << "Quality " << image_quality; break;
        }
        return ts.str();
    }
    // Composite: (file_type << 16) | quality.
    std::uint16_t file_type = (image_quality >> 16) & 0xFFFF;
    std::uint16_t quality = image_quality & 0xFFFF;
    switch (file_type) {
    case SDK::CrFileType_Jpeg:    ts << "JPEG"; break;
    case SDK::CrFileType_Raw:     ts << "RAW"; break;
    case SDK::CrFileType_RawJpeg: ts << "RAW+JPEG"; break;
    case SDK::CrFileType_RawHeif: ts << "RAW+HEIF"; break;
    case SDK::CrFileType_Heif:    ts << "HEIF"; break;
    default:                      ts << "Unknown"; break;
    }
    ts << " ";
    switch (quality) {
    case SDK::CrImageQuality_Light:    ts << "Light"; break;
    case SDK::CrImageQuality_Standard: ts << "Standard"; break;
    case SDK::CrImageQuality_Fine:     ts << "Fine"; break;
    case SDK::CrImageQuality_ExFine:   ts << "Extra Fine"; break;
    default:                           ts << "Unknown Quality"; break;
    }
    return ts.str();
}

text format_raw_file_compression(std::uint16_t raw_compression) {
    text_stringstream ts;
    switch (raw_compression) {
    case SDK::CrRAWFile_Uncompression: ts << "Uncompressed"; break;
    case SDK::CrRAWFile_Compression:   ts << "Compressed"; break;
    case SDK::CrRAWFile_LossLess:      ts << "Lossless"; break;
    case SDK::CrRAWFile_LossLessS:     ts << "Lossless S"; break;
    case SDK::CrRAWFile_LossLessM:     ts << "Lossless M"; break;
    case SDK::CrRAWFile_LossLessL:     ts << "Lossless L"; break;
    default:                           ts << "Unknown(" << raw_compression << ")"; break;
    }
    return ts.str();
}

text format_still_image_store_destination(std::uint16_t store_destination) {
    text_stringstream ts;
    switch (store_destination) {
    case SDK::CrStillImageStoreDestination_HostPC:              ts << "Host PC"; break;
    case SDK::CrStillImageStoreDestination_MemoryCard:          ts << "Memory Card"; break;
    case SDK::CrStillImageStoreDestination_HostPCAndMemoryCard: ts << "Host PC & Memory Card"; break;
    default:                                                    ts << "Unknown(" << store_destination << ")"; break;
    }
    return ts.str();
}

// --- Parsers -------------------------------------------------------------
std::uint16_t parse_position_key_setting(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "camera" || n == "camera-position") return SDK::CrPriorityKey_CameraPosition;
    if (n == "pc-remote" || n == "pcremote" || n == "pc") return SDK::CrPriorityKey_PCRemote;
    throw std::invalid_argument("Unknown priority key setting: '" + value +
                                "'. Valid values: 'camera', 'pc-remote'");
}

std::uint16_t parse_still_image_store_destination(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "host-pc" || n == "hostpc" || n == "pc" || n == "host")
        return SDK::CrStillImageStoreDestination_HostPC;
    if (n == "memory-card" || n == "memorycard" || n == "card" || n == "camera")
        return SDK::CrStillImageStoreDestination_MemoryCard;
    if (n == "host-pc-and-memory-card" || n == "hostpc-and-memorycard" ||
        n == "both" || n == "pc-and-card" || n == "host-and-camera")
        return SDK::CrStillImageStoreDestination_HostPCAndMemoryCard;
    throw std::invalid_argument("Unknown store destination: '" + value +
                                "'. Valid values: 'host-pc', 'memory-card', 'host-pc-and-memory-card'");
}

std::uint16_t parse_white_balance(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "awb" || n == "auto") return SDK::CrWhiteBalance_AWB;
    if (n == "underwater-auto") return SDK::CrWhiteBalance_Underwater_Auto;
    if (n == "daylight") return SDK::CrWhiteBalance_Daylight;
    if (n == "shadow") return SDK::CrWhiteBalance_Shadow;
    if (n == "cloudy") return SDK::CrWhiteBalance_Cloudy;
    if (n == "tungsten") return SDK::CrWhiteBalance_Tungsten;
    if (n == "fluorescent") return SDK::CrWhiteBalance_Fluorescent;
    if (n == "fluorescent-warmwhite" || n == "fluorescent-warm-white") return SDK::CrWhiteBalance_Fluorescent_WarmWhite;
    if (n == "fluorescent-coolwhite" || n == "fluorescent-cool-white") return SDK::CrWhiteBalance_Fluorescent_CoolWhite;
    if (n == "fluorescent-daywhite" || n == "fluorescent-day-white") return SDK::CrWhiteBalance_Fluorescent_DayWhite;
    if (n == "fluorescent-daylight") return SDK::CrWhiteBalance_Fluorescent_Daylight;
    if (n == "flush" || n == "flash") return SDK::CrWhiteBalance_Flush;
    if (n == "colortemp" || n == "color-temp") return SDK::CrWhiteBalance_ColorTemp;
    if (n == "custom-1" || n == "custom1") return SDK::CrWhiteBalance_Custom_1;
    if (n == "custom-2" || n == "custom2") return SDK::CrWhiteBalance_Custom_2;
    if (n == "custom-3" || n == "custom3") return SDK::CrWhiteBalance_Custom_3;
    if (n == "custom") return SDK::CrWhiteBalance_Custom;
    throw std::invalid_argument("Unknown white balance: '" + value +
                                "'. Valid values: 'auto', 'daylight', 'cloudy', 'tungsten', 'fluorescent', 'flash', 'custom-1', etc.");
}

std::uint16_t parse_focus_mode(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "mf" || n == "manual") return SDK::CrFocus_MF;
    if (n == "af-s" || n == "afs") return SDK::CrFocus_AF_S;
    if (n == "af-c" || n == "afc") return SDK::CrFocus_AF_C;
    if (n == "af-a" || n == "afa") return SDK::CrFocus_AF_A;
    if (n == "af-d" || n == "afd") return SDK::CrFocus_AF_D;
    if (n == "dmf") return SDK::CrFocus_DMF;
    if (n == "pf") return SDK::CrFocus_PF;
    throw std::invalid_argument("Unknown focus mode: '" + value +
                                "'. Valid values: 'mf', 'af-s', 'af-c', 'af-a', 'dmf', 'pf'");
}

std::uint16_t parse_focus_area(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "wide") return SDK::CrFocusArea_Wide;
    if (n == "zone") return SDK::CrFocusArea_Zone;
    if (n == "center" || n == "centre") return SDK::CrFocusArea_Center;
    if (n == "flexible-spot-s" || n == "flex-s" || n == "spot-s") return SDK::CrFocusArea_Flexible_Spot_S;
    if (n == "flexible-spot-m" || n == "flex-m" || n == "spot-m") return SDK::CrFocusArea_Flexible_Spot_M;
    if (n == "flexible-spot-l" || n == "flex-l" || n == "spot-l") return SDK::CrFocusArea_Flexible_Spot_L;
    if (n == "expand-flexible-spot" || n == "expand-flex" || n == "expand") return SDK::CrFocusArea_Expand_Flexible_Spot;
    throw std::invalid_argument("Unknown focus area: '" + value +
                                "'. Valid values: 'wide', 'zone', 'center', 'flexible-spot-s', 'flexible-spot-m', 'flexible-spot-l', 'expand-flexible-spot'");
}

std::uint16_t parse_raw_file_compression(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "uncompressed" || n == "uncompressed-raw" || n == "none") return SDK::CrRAWFile_Uncompression;
    if (n == "compressed" || n == "compressed-raw") return SDK::CrRAWFile_Compression;
    if (n == "lossless" || n == "lossless-compressed") return SDK::CrRAWFile_LossLess;
    if (n == "lossless-s" || n == "lossless-small") return SDK::CrRAWFile_LossLessS;
    if (n == "lossless-m" || n == "lossless-medium") return SDK::CrRAWFile_LossLessM;
    if (n == "lossless-l" || n == "lossless-large") return SDK::CrRAWFile_LossLessL;
    throw std::invalid_argument("Unknown RAW compression: '" + value +
                                "'. Valid values: 'uncompressed', 'compressed', 'lossless', 'lossless-s', 'lossless-m', 'lossless-l'");
}

std::uint32_t parse_exposure_program_mode(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "m" || n == "manual" || n == "m-manual") return SDK::CrExposure_M_Manual;
    if (n == "p" || n == "auto" || n == "p-auto") return SDK::CrExposure_P_Auto;
    if (n == "a" || n == "aperture" || n == "a-aperturepriority") return SDK::CrExposure_A_AperturePriority;
    if (n == "s" || n == "shutter" || n == "s-shutterspeedpriority") return SDK::CrExposure_S_ShutterSpeedPriority;
    if (n == "program-creative" || n == "programcreative") return SDK::CrExposure_Program_Creative;
    if (n == "program-action" || n == "programaction") return SDK::CrExposure_Program_Action;
    if (n == "portrait") return SDK::CrExposure_Portrait;
    if (n == "auto-plus" || n == "autoplus") return SDK::CrExposure_Auto_Plus;
    if (n == "p-a" || n == "pa") return SDK::CrExposure_P_A;
    if (n == "p-s" || n == "ps") return SDK::CrExposure_P_S;
    if (n == "sports" || n == "sports-action") return SDK::CrExposure_Sports_Action;
    if (n == "sunset") return SDK::CrExposure_Sunset;
    if (n == "night") return SDK::CrExposure_Night;
    if (n == "landscape") return SDK::CrExposure_Landscape;
    if (n == "macro") return SDK::CrExposure_Macro;
    throw std::invalid_argument("Unknown exposure program mode: '" + value +
                                "'. Valid values: 'm', 'p', 'a', 's', 'portrait', 'landscape', 'sports', etc.");
}

std::uint32_t parse_still_capture_mode(const std::string& value) {
    std::string n = normalize_string(value);
    if (n == "single") return SDK::CrDrive_Single;
    if (n == "continuous-hi" || n == "continuous-high") return SDK::CrDrive_Continuous_Hi;
    if (n == "continuous-hi-plus" || n == "continuous-high-plus") return SDK::CrDrive_Continuous_Hi_Plus;
    if (n == "continuous-hi-live" || n == "continuous-high-live") return SDK::CrDrive_Continuous_Hi_Live;
    if (n == "continuous-lo" || n == "continuous-low") return SDK::CrDrive_Continuous_Lo;
    if (n == "continuous") return SDK::CrDrive_Continuous;
    if (n == "continuous-speedpriority" || n == "continuous-speed-priority") return SDK::CrDrive_Continuous_SpeedPriority;
    if (n == "continuous-mid" || n == "continuous-medium") return SDK::CrDrive_Continuous_Mid;
    if (n == "continuous-mid-live" || n == "continuous-medium-live") return SDK::CrDrive_Continuous_Mid_Live;
    if (n == "continuous-lo-live" || n == "continuous-low-live") return SDK::CrDrive_Continuous_Lo_Live;
    if (n == "timelapse") return SDK::CrDrive_Timelapse;
    if (n == "timer-2s" || n == "timer2s" || n == "timer-2") return SDK::CrDrive_Timer_2s;
    if (n == "timer-5s" || n == "timer5s" || n == "timer-5") return SDK::CrDrive_Timer_5s;
    if (n == "timer-10s" || n == "timer10s" || n == "timer-10") return SDK::CrDrive_Timer_10s;
    if (n == "continuous-bracket-03ev-3pics") return SDK::CrDrive_Continuous_Bracket_03Ev_3pics;
    if (n == "continuous-bracket-05ev-3pics") return SDK::CrDrive_Continuous_Bracket_05Ev_3pics;
    if (n == "single-bracket-03ev-3pics") return SDK::CrDrive_Single_Bracket_03Ev_3pics;
    if (n == "focus-bracket" || n == "focusbracket" || n == "focus-bracketing") return SDK::CrDrive_FocusBracket;
    throw std::invalid_argument("Unknown drive mode: '" + value +
                                "'. Valid values: 'single', 'continuous-hi', 'continuous-lo', 'timer-2s', 'timelapse', 'focus-bracket', etc.");
}

std::uint32_t parse_shutter_speed(const std::string& value) {
    std::string normalized = normalize_string(value);
    if (normalized == "bulb") return SDK::CrShutterSpeed_Bulb;
    if (value.length() >= 2 && (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X")) {
        return static_cast<std::uint32_t>(std::stoull(value.substr(2), nullptr, 16));
    }
    if (value.find('/') != std::string::npos) {
        auto slashPos = value.find('/');
        CrInt16u numerator = static_cast<CrInt16u>(std::stoi(value.substr(0, slashPos)));
        CrInt16u denominator = static_cast<CrInt16u>(std::stoi(value.substr(slashPos + 1)));
        return (static_cast<std::uint32_t>(numerator) << 16) | denominator;
    }
    if (normalized.back() == 's' || value.back() == '"') {
        std::string numStr = value;
        numStr.pop_back();
        if (numStr.find('.') != std::string::npos) {
            throw std::invalid_argument("Decimal second shutter speeds ('" + value +
                "') are ambiguous. Use the hex value from available_values instead.");
        }
        CrInt16u numerator = static_cast<CrInt16u>(std::stoi(numStr));
        CrInt16u denominator = 1;
        return (static_cast<std::uint32_t>(numerator) << 16) | denominator;
    }
    try {
        int val = std::stoi(value);
        if (val > 0) return (static_cast<std::uint32_t>(1) << 16) | static_cast<CrInt16u>(val);
    } catch (...) {}
    throw std::invalid_argument("Invalid shutter speed: '" + value +
        "'. Valid formats: '1/125', '2s', 'bulb', or hex value from available_values (e.g., '0x0001007D')");
}

std::uint16_t parse_file_type(const std::string& value) {
    std::string normalized = normalize_string(value);
    std::replace(normalized.begin(), normalized.end(), '+', '-');

    if (normalized == "none") return SDK::CrFileType_None;
    if (normalized == "jpeg" || normalized == "jpg") return SDK::CrFileType_Jpeg;
    if (normalized == "raw") return SDK::CrFileType_Raw;
    if (normalized == "raw-jpeg" || normalized == "rawjpeg") return SDK::CrFileType_RawJpeg;
    if (normalized == "raw-heif" || normalized == "rawheif") return SDK::CrFileType_RawHeif;
    if (normalized == "heif") return SDK::CrFileType_Heif;

    throw std::invalid_argument("Unknown file format: '" + value +
                                "'. Valid values: 'jpeg', 'raw', 'raw+jpeg', 'raw+heif', 'heif'");
}

std::uint32_t parse_still_image_quality(const std::string& value) {
    std::string normalized = normalize_string(value);
    std::replace(normalized.begin(), normalized.end(), '+', '-');

    auto parseQualityOnly = [](const std::string& qualityToken) -> std::uint16_t {
        if (qualityToken == "light") return SDK::CrImageQuality_Light;
        if (qualityToken == "standard") return SDK::CrImageQuality_Standard;
        if (qualityToken == "fine") return SDK::CrImageQuality_Fine;
        if (qualityToken == "extra-fine") return SDK::CrImageQuality_ExFine;
        throw std::invalid_argument("");
    };

    try {
        return parseQualityOnly(normalized);
    } catch (const std::invalid_argument&) {
    }

    // File-type-qualified form, e.g. "raw+jpeg fine" -> (fileType << 16) | quality.
    const std::pair<const char*, std::uint16_t> fileTypes[] = {
        {"raw-jpeg-", SDK::CrFileType_RawJpeg},
        {"raw-heif-", SDK::CrFileType_RawHeif},
        {"jpeg-", SDK::CrFileType_Jpeg},
        {"raw-", SDK::CrFileType_Raw},
        {"heif-", SDK::CrFileType_Heif},
    };

    for (const auto& [prefix, fileType] : fileTypes) {
        std::string prefixStr(prefix);
        if (normalized.rfind(prefixStr, 0) == 0) {
            std::string qualityToken = normalized.substr(prefixStr.size());
            try {
                std::uint16_t quality = parseQualityOnly(qualityToken);
                return (static_cast<std::uint32_t>(fileType) << 16) | quality;
            } catch (const std::invalid_argument&) {
                break;
            }
        }
    }

    throw std::invalid_argument("Unknown image quality: '" + value +
        "'. Valid values: 'light', 'standard', 'fine', 'extra fine', 'jpeg extra fine', 'raw+jpeg fine', etc.");
}

}  // namespace cli
