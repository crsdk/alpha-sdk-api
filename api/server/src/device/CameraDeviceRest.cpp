// CameraDeviceRest — MIT-licensed, non-interactive Sony camera device layer.
// See CameraDeviceRest.h for the design rationale. This is original REST-server
// code that calls the Sony SDK directly; it contains none of Sony's sample
// source. Behavior is ported from the previous forked shared/core/CameraDevice,
// with all interactive (stdin/stdout) paths removed and all state owned locally.

// On Windows, include <windows.h> before any project header. Sony's stock
// Text.h redefines the Windows TCHAR/TEXT macros (this project builds char-
// based, no UNICODE), which corrupts <winnt.h> when it is pulled in afterwards
// via <filesystem>. Including <windows.h> first lets winnt.h process cleanly.
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "CameraDeviceRest.h"
#include "CrDebugString.h"  // stock: CrErrorString, for the generic warning event
#include "JsonEscape.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <system_error>

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>   // getcwd
#endif

namespace SDK = SCRSDK;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
constexpr int kImageSaveAutoStartNo = -1;

// Snapshot image/movie files in a directory (transfer-polling fallback).
std::set<std::string> snapshotImageFiles(const std::string& dir) {
    std::set<std::string> files;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".arw" ||
            ext == ".heif" || ext == ".hif" || ext == ".mp4") {
            files.insert(entry.path().filename().string());
        }
    }
    return files;
}
}  // namespace

namespace rest {

CameraDeviceRest::CameraDeviceRest(std::int32_t no,
                                   SCRSDK::ICrCameraObjectInfo const* camera_info)
    : m_number(no)
    , m_prop() {
    m_info = SDK::CreateCameraObjectInfo(
        camera_info->GetName(),
        camera_info->GetModel(),
        camera_info->GetUsbPid(),
        camera_info->GetIdType(),
        camera_info->GetIdSize(),
        camera_info->GetId(),
        camera_info->GetConnectionTypeName(),
        camera_info->GetAdaptorName(),
        camera_info->GetPairingNecessity(),
        camera_info->GetSSHsupport());
    m_connType = cli::parse_connection_type(m_info->GetConnectionTypeName());
    m_modeSDK = SCRSDK::CrSdkControlMode_Remote;
}

CameraDeviceRest::~CameraDeviceRest() {
    stopTransferPolling();
    release_contents_info(0);
    release_contents_info(1);
    if (m_info) m_info->Release();
}

// --- Connection lifecycle (non-interactive) ------------------------------
// Credentials are supplied by the caller rather than prompted for; the
// interactive stdin prompts from the SDK sample are intentionally omitted.
//
// A body with access authentication switched on needs all three of userId,
// password and fingerprint. Everything else — USB, and networked bodies with
// authentication off — passes none of them and takes the path below unchanged:
// the SDK still wants a non-null user id, so the historical "admin" default
// stands in, with an empty password and a zero-length fingerprint.
bool CameraDeviceRest::connect(SCRSDK::CrSdkControlMode openMode,
                               SCRSDK::CrReconnectingSet reconnect,
                               const std::string& userId,
                               const std::string& password,
                               const std::string& fingerprint) {
    m_modeSDK = openMode;
    m_lastError.store(0);
    const char* inputId = userId.empty() ? "admin" : userId.c_str();
    auto status = SDK::Connect(m_info, this, &m_deviceHandle, openMode, reconnect,
                               inputId, password.c_str(), fingerprint.c_str(),
                               static_cast<CrInt32u>(fingerprint.size()));
    if (CR_FAILED(status)) {
        return false;
    }
    // Default save destination to the current working directory.
    set_save_info(".", "", kImageSaveAutoStartNo, nullptr);
    return true;
}

bool CameraDeviceRest::ssh_supported() const {
    return m_info && m_info->GetSSHsupport() == SDK::CrSSHsupportValue::CrSSHsupport_ON;
}

std::string CameraDeviceRest::get_fingerprint() {
    // Buffer size and the (buf, len) construction follow the SDK sample; the
    // returned bytes are not guaranteed NUL-terminated, so build with the length.
    CrInt32u fpLen = 0;
    char fpBuff[128] = {0};
    if (CR_FAILED(SDK::GetFingerprint(m_info, fpBuff, &fpLen)) || fpLen == 0) return "";
    return std::string(fpBuff, fpLen);
}

bool CameraDeviceRest::wait_for_connection(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_connected.load()) return true;
        // A reported error is terminal — no point waiting out the timeout.
        if (m_lastError.load() != 0) return false;
        std::this_thread::sleep_for(100ms);
    }
    return m_connected.load();
}

bool CameraDeviceRest::disconnect() {
    if (m_deviceHandle == 0) return true;
    auto status = SDK::Disconnect(m_deviceHandle);
    return !CR_FAILED(status);
}

bool CameraDeviceRest::release() {
    if (m_deviceHandle == 0) return true;
    auto status = SDK::ReleaseDevice(m_deviceHandle);
    m_deviceHandle = 0;
    return !CR_FAILED(status);
}

// Decode a string field returned by the Sony SDK into a narrow UTF-8 string.
//
// Why this exists: this server builds char-based (no UNICODE), so cli::text is
// std::string and the SDK's CrChar* fields are typed char*. On macOS/Linux the
// SDK genuinely returns narrow UTF-8, so a plain read works. But the precompiled
// *Windows* Cr_Core returns these fields as UTF-16LE regardless of the app's
// non-UNICODE build, so reading std::string((char*)GetModel()) or
// std::string((char*)GetId()) stops at the first 0x00 high byte and truncates
// model/id to their first character ("I", "C"). This decoder reads the sized
// buffer width-aware so every platform yields the full value.
//
//   buf          raw buffer (CrChar* or id data buffer)
//   sizeBytes    buffer length in bytes (GetModelSize()/GetIdSize()); typically
//                includes the NUL terminator
//   hexIfBinary  when true, a non-printable narrow buffer is rendered as
//                uppercase hex (used for the id, whose data type may be binary)
static std::string decode_sdk_string(const void* buf, CrInt32u sizeBytes,
                                     bool hexIfBinary) {
    const CrInt8u* b = static_cast<const CrInt8u*>(buf);
    if (!b || sizeBytes == 0) {
        return std::string();
    }

    // Detect UTF-16LE with ASCII payload: even byte count and every high byte
    // (odd index) is 0x00. Model strings and camera ids are ASCII, so this is
    // unambiguous versus a narrow buffer (which has no interior NULs).
    bool utf16le = (sizeBytes >= 2) && (sizeBytes % 2 == 0);
    if (utf16le) {
        for (CrInt32u i = 1; i < sizeBytes; i += 2) {
            if (b[i] != 0x00) { utf16le = false; break; }
        }
    }

    std::string out;
    if (utf16le) {
        for (CrInt32u i = 0; i + 1 < sizeBytes; i += 2) {
            if (b[i] == 0x00) break;                    // NUL terminator
            out.push_back(static_cast<char>(b[i]));
        }
        return out;
    }

    // Narrow buffer.
    bool printable = true;
    for (CrInt32u i = 0; i < sizeBytes; ++i) {
        if (b[i] == 0x00) break;
        if (b[i] < 0x20 || b[i] > 0x7E) { printable = false; break; }
    }
    if (printable || !hexIfBinary) {
        for (CrInt32u i = 0; i < sizeBytes; ++i) {
            if (b[i] == 0x00) break;
            out.push_back(static_cast<char>(b[i]));
        }
    } else {
        // Genuinely binary id data — render as stable uppercase hex.
        static const char* kHex = "0123456789ABCDEF";
        for (CrInt32u i = 0; i < sizeBytes; ++i) {
            out.push_back(kHex[(b[i] >> 4) & 0xF]);
            out.push_back(kHex[b[i] & 0xF]);
        }
    }
    return out;
}

cli::text CameraDeviceRest::get_model() const {
    return cli::text(decode_sdk_string(m_info->GetModel(),
                                       m_info->GetModelSize(),
                                       /*hexIfBinary=*/false).c_str());
}

cli::text CameraDeviceRest::get_id() const {
    if (cli::ConnectionType::NETWORK == m_connType) {
        return m_info->GetMACAddressChar();
    }
    return cli::text(decode_sdk_string(m_info->GetId(),
                                       m_info->GetIdSize(),
                                       /*hexIfBinary=*/true).c_str());
}

// --- Internal single-property helpers ------------------------------------
void CameraDeviceRest::get_property(SCRSDK::CrDeviceProperty& prop) const {
    SCRSDK::CrDeviceProperty* properties = nullptr;
    CrInt32 nprops = 0;
    CrInt32u code = prop.GetCode();
    if (CR_SUCCEEDED(SDK::GetSelectDeviceProperties(m_deviceHandle, 1, &code,
                                                    &properties, &nprops)) &&
        nprops == 1 && properties) {
        prop = properties[0];
        SDK::ReleaseDeviceProperties(m_deviceHandle, properties);
    }
}

bool CameraDeviceRest::set_property(SCRSDK::CrDeviceProperty& prop) const {
    return !CR_FAILED(SDK::SetDeviceProperty(m_deviceHandle, &prop));
}

cli::text CameraDeviceRest::getCurrentStr(SCRSDK::CrDeviceProperty* prop) {
    if (nullptr == prop) return cli::text("target pointer is null");
    if (SCRSDK::CrDataType_STR != prop->GetValueType()) return cli::text("target is not CrDataType_STR");
    auto enableFlag = prop->GetPropertyEnableFlag();
    if (enableFlag == SDK::CrEnableValue_True || enableFlag == SDK::CrEnableValue_DisplayOnly) {
        CrInt16u* pCurrentStr = prop->GetCurrentStr();
        if (pCurrentStr) {
            int length = (int)*pCurrentStr;
            char buff[128];
            memset(buff, 0, sizeof(buff));
            pCurrentStr++;
            for (int i = 0; i < (length - 1) && i < 127; ++i, ++pCurrentStr) {
                wctomb(&buff[i], (wchar_t)*pCurrentStr);
            }
            return cli::text((CrChar*)buff).c_str();
        }
        return cli::text("(blank)");
    }
    return cli::text("-");
}

// Fetch a single property's current value. Returns true if the camera reported
// the property (i.e. it is supported on this body).
static bool fetchOne(std::int64_t handle, CrInt32u code, CrInt64u& value) {
    value = 0;
    CrInt32 nprop = 0;
    SCRSDK::CrDeviceProperty* list = nullptr;
    auto res = SDK::GetSelectDeviceProperties(handle, 1, &code, &list, &nprop);
    bool ok = false;
    if (CR_SUCCEEDED(res) && nprop == 1 && list) {
        if (list[0].GetCode() == code) {
            value = list[0].GetCurrentValue();
            ok = true;
        }
        SDK::ReleaseDeviceProperties(handle, list);
    }
    return ok;
}

// --- Drive mode / priority key (parameterized, non-interactive) ----------
bool CameraDeviceRest::set_drive_mode(CrInt64u value) {
    if (!m_deviceHandle || !is_connected()) return false;
    SDK::CrDeviceProperty mode;
    mode.SetCode(SDK::CrDeviceProperty_DriveMode);
    mode.SetCurrentValue(value);
    mode.SetValueType(SDK::CrDataType_UInt32Array);
    if (CR_FAILED(SDK::SetDeviceProperty(m_deviceHandle, &mode))) return false;
    // Verify the camera applied the drive mode.
    std::this_thread::sleep_for(500ms);
    CrInt64u applied = 0;
    fetchOne(m_deviceHandle, SDK::CrDeviceProperty_DriveMode, applied);
    return applied == value;
}

bool CameraDeviceRest::set_exposure_program_mode(CrInt64u value) {
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_ExposureProgramMode);
    prop.SetCurrentValue(value);
    prop.SetValueType(SDK::CrDataType_UInt16Array);
    return !CR_FAILED(SDK::SetDeviceProperty(m_deviceHandle, &prop));
}

bool CameraDeviceRest::set_focus_mode(CrInt64u value) {
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_FocusMode);
    prop.SetCurrentValue(value);
    prop.SetValueType(SDK::CrDataType_UInt16Array);
    return !CR_FAILED(SDK::SetDeviceProperty(m_deviceHandle, &prop));
}

bool CameraDeviceRest::set_focus_area(CrInt64u value) {
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_FocusArea);
    prop.SetCurrentValue(value);
    prop.SetValueType(SDK::CrDataType_UInt16Array);
    return !CR_FAILED(SDK::SetDeviceProperty(m_deviceHandle, &prop));
}

bool CameraDeviceRest::set_priority_key_to_pc_remote() {
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_PriorityKeySettings);
    prop.SetCurrentValue(SDK::CrPriorityKey_PCRemote);
    prop.SetValueType(SDK::CrDataType_UInt16Array);
    return CR_SUCCEEDED(SDK::SetDeviceProperty(m_deviceHandle, &prop));
}

// --- Shooting actions ----------------------------------------------------
void CameraDeviceRest::capture_image() const {
    SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_Release, SDK::CrCommandParam_Down);
    std::this_thread::sleep_for(500ms);
    SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_Release, SDK::CrCommandParam_Up);
}

void CameraDeviceRest::shutter_down() const {
    SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_Release, SDK::CrCommandParam_Down);
}

void CameraDeviceRest::shutter_up() const {
    SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_Release, SDK::CrCommandParam_Up);
}

bool CameraDeviceRest::af_shutter() const {
    // Refuse in MF; the SDK sample prompted the user — we check programmatically.
    CrInt64u focusMode = 0;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_FocusMode, focusMode) ||
        focusMode == SDK::CrFocus_MF) {
        return false;
    }
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_S1);
    prop.SetCurrentValue(SDK::CrLockIndicator_Locked);
    prop.SetValueType(SDK::CrDataType_UInt16);
    SDK::SetDeviceProperty(m_deviceHandle, &prop);

    std::this_thread::sleep_for(500ms);
    SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_Release, SDK::CrCommandParam_Down);
    std::this_thread::sleep_for(100ms);
    SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_Release, SDK::CrCommandParam_Up);
    std::this_thread::sleep_for(1s);
    prop.SetCurrentValue(SDK::CrLockIndicator_Unlocked);
    SDK::SetDeviceProperty(m_deviceHandle, &prop);
    return true;
}

bool CameraDeviceRest::toggle_movie_recording_direct() {
    CrInt64u state = 0;
    fetchOne(m_deviceHandle, SDK::CrDeviceProperty_RecordingState, state);
    bool isRecording = (state == SDK::CrMovie_Recording_State_Recording);
    auto param = isRecording ? SDK::CrCommandParam_Up : SDK::CrCommandParam_Down;
    auto err = SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_MovieRecord, param);
    return !CR_FAILED(err);
}

void CameraDeviceRest::s1_shooting() const { s1_shooting_non_interactive(); }

void CameraDeviceRest::s1_shooting_non_interactive() const {
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_S1);
    prop.SetCurrentValue(SDK::CrLockIndicator_Locked);
    prop.SetValueType(SDK::CrDataType_UInt16);
    SDK::SetDeviceProperty(m_deviceHandle, &prop);
    std::this_thread::sleep_for(1s);
    prop.SetCurrentValue(SDK::CrLockIndicator_Unlocked);
    SDK::SetDeviceProperty(m_deviceHandle, &prop);
}

// --- Contents / remote-transfer file browsing ----------------------------
// ContentsTransfer (MTP) and RemoteTransfer file listing/download. Ported
// non-interactively from the SDK sample: the enumeration is preserved, the
// sample's interactive stdin download menu is omitted (unusable server-side).

// ContentsTransfer (MTP): enumerate date folders -> contents -> file entries.
CameraDeviceRest::MtpContentsListResult CameraDeviceRest::list_contents_transfer_files() {
    MtpContentsListResult result;

    CrInt64u status = 0;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_ContentsTransferStatus, status) ||
        status != SDK::CrContentsTransfer_ON) {
        result.error_message = "Contents transfer is not enabled on the camera.";
        return result;
    }

    CrInt32u f_nums = 0;
    SDK::CrMtpFolderInfo* f_list = nullptr;
    SDK::CrError err = SDK::GetDateFolderList(m_deviceHandle, &f_list, &f_nums);
    if (CR_FAILED(err)) {
        result.error_message = "Failed to get folder list from SD card.";
        return result;
    }
    if (f_nums == 0 || !f_list) {
        if (f_list) SDK::ReleaseDateFolderList(m_deviceHandle, f_list);
        result.success = true;
        result.error_message = "No contents found on SD card";
        return result;
    }

    for (CrInt32u fi = 0; fi < f_nums; ++fi) {
        CrInt32u c_nums = 0;
        SDK::CrContentHandle* c_list = nullptr;
        err = SDK::GetContentsHandleList(m_deviceHandle, f_list[fi].handle, &c_list, &c_nums);
        if (CR_SUCCEEDED(err) && c_nums > 0 && c_list) {
            for (CrInt32u ci = 0; ci < c_nums; ++ci) {
                auto* info = new SDK::CrMtpContentsInfo();
                if (CR_SUCCEEDED(SDK::GetContentsDetailInfo(m_deviceHandle, c_list[ci], info))) {
                    MtpFileEntry entry;
                    entry.handle = static_cast<uint32_t>(info->handle);
                    entry.fileSize = info->contentSize;
                    entry.width = info->width;
                    entry.height = info->height;
                    if (info->fileName && info->fileNameSize > 0) {
                        cli::text fname(info->fileName);
                        entry.fileName = std::string(fname.data());
                    }
                    for (int i = 0; i < 16 && info->dateChar[i]; ++i) {
                        entry.date += static_cast<char>(info->dateChar[i]);
                    }
                    result.files.push_back(entry);
                }
                delete info;
            }
            SDK::ReleaseContentsHandleList(m_deviceHandle, c_list);
        }
    }
    SDK::ReleaseDateFolderList(m_deviceHandle, f_list);

    result.success = true;
    if (result.files.empty()) result.error_message = "No contents found on SD card";
    return result;
}

// RemoteTransfer: fetch the SDK content-info list for a slot.
CameraDeviceRest::ContentsListResult CameraDeviceRest::list_remote_transfer_contents(int slot_number) {
    ContentsListResult result;

    SDK::CrSlotNumber slotNumber;
    int slotIndex;
    if (slot_number == 1) { slotNumber = SDK::CrSlotNumber_Slot1; slotIndex = 0; }
    else if (slot_number == 2) { slotNumber = SDK::CrSlotNumber_Slot2; slotIndex = 1; }
    else { result.error_message = "Invalid slot number. Use 1 or 2."; return result; }

    release_contents_info(slotIndex);

    // Get all contents from the slot. CrGetContentsInfoListType_All returns every
    // content regardless of the (ignored) date argument.
    SDK::CrCaptureDate dummyCaptureDate;
    CrInt32u contentsInfoListNum = 0;
    SDK::CrError ret = SDK::GetRemoteTransferContentsInfoList(
        m_deviceHandle, slotNumber, SDK::CrGetContentsInfoListType_All,
        &dummyCaptureDate, 0, &m_contentsInfoList[slotIndex], &contentsInfoListNum);

    if (ret != SDK::CrError_None) {
        result.error_message = "Failed to get contents list from SD card";
        release_contents_info(slotIndex);
        return result;
    }
    if (contentsInfoListNum == 0) {
        result.success = true;
        result.error_message = "No contents found on SD card";
        release_contents_info(slotIndex);
        return result;
    }
    for (CrInt32u i = 0; i < contentsInfoListNum; ++i) {
        result.contents.push_back(m_contentsInfoList[slotIndex][i]);
    }
    result.success = true;
    return result;
}

// ContentsTransfer download: async PullContentsFile by MTP handle.
CameraDeviceRest::FileDownloadResult CameraDeviceRest::download_contents_transfer_file(
    CrInt32u content_handle, const std::string& save_path) {
    FileDownloadResult result;
    if (!save_path.empty()) set_save_info(save_path, "", 0, nullptr);

    SDK::CrError err = SDK::PullContentsFile(m_deviceHandle,
                                             static_cast<SDK::CrContentHandle>(content_handle));
    if (SDK::CrError_None != err) {
        char hex[32];
        snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(err));
        result.error_message = std::string("Failed to start file download: ") + hex;
    } else {
        result.success = true;
        result.message = "Download started (contents-transfer)";
    }
    return result;
}

// RemoteTransfer download: async GetRemoteTransferContentsDataFile; progress is
// delivered via OnNotifyRemoteTransferResult (SSE), with a disk-polling fallback.
CameraDeviceRest::FileDownloadResult CameraDeviceRest::download_remote_transfer_file(
    int slot_number, CrInt32u content_id, CrInt32u file_id, const std::string& save_path) {
    FileDownloadResult result;

    SDK::CrSlotNumber slotNumber;
    if (slot_number == 1) slotNumber = SDK::CrSlotNumber_Slot1;
    else if (slot_number == 2) slotNumber = SDK::CrSlotNumber_Slot2;
    else { result.error_message = "Invalid slot number. Use 1 or 2."; return result; }

    CrInt32u divisionSize = 0x5000000;  // 80MB
#if defined(__linux__)
    if (m_connType == cli::ConnectionType::USB) divisionSize = 0x1000000;  // 16MB
#endif

    if (!save_path.empty()) set_save_info(save_path, "", -1, nullptr);

    m_getContentsDataStartFlg = true;
    std::string effectiveSaveDir = save_path.empty() ? m_savePath : save_path;
    auto preSnapshot = snapshotImageFiles(effectiveSaveDir);

    SDK::CrError ret = SDK::GetRemoteTransferContentsDataFile(
        m_deviceHandle, slotNumber, content_id, file_id, divisionSize, nullptr, nullptr);

    if (ret != SDK::CrError_None) {
        char hex[32];
        snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(ret));
        result.error_message = std::string("Failed to start file download: ") + hex;
        m_getContentsDataStartFlg = false;
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
        m_pendingTransfers.push_back(
            {effectiveSaveDir, std::move(preSnapshot), std::chrono::steady_clock::now(),
             static_cast<unsigned int>(content_id), static_cast<unsigned int>(file_id)});
    }
    m_inFlightContentId.store(static_cast<unsigned int>(content_id));
    m_inFlightFileId.store(static_cast<unsigned int>(file_id));
    {
    }
    startTransferPolling();

    result.success = true;
    result.message = "Download started (remote-transfer)";
    return result;
}

// RemoteTransfer compressed (thumbnail/screennail) downloads: async
// GetRemoteTransferContentsCompressedDataFile. Progress is delivered via
// OnNotifyRemoteTransferResult (SSE), with the same disk-polling fallback as
// the full-file path.
CameraDeviceRest::FileDownloadResult CameraDeviceRest::download_remote_transfer_thumbnail(
    int slot_number, CrInt32u content_id, CrInt32u file_id, const std::string& save_path) {
    FileDownloadResult result;
    result.success = false;
    result.progress_percent = 0;

    SDK::CrSlotNumber slotNumber;
    if (slot_number == 1) slotNumber = SDK::CrSlotNumber_Slot1;
    else if (slot_number == 2) slotNumber = SDK::CrSlotNumber_Slot2;
    else { result.error_message = "Invalid slot number. Use 1 or 2."; return result; }

    if (!save_path.empty()) set_save_info(save_path, "", -1, nullptr);

    m_getContentsDataStartFlg = true;
    std::string effectiveSaveDir = save_path.empty() ? m_savePath : save_path;
    auto preSnapshot = snapshotImageFiles(effectiveSaveDir);

    SDK::CrError ret = SDK::GetRemoteTransferContentsCompressedDataFile(
        m_deviceHandle, slotNumber, content_id, file_id,
        SDK::CrGetContentsCompressedDataType_Thumbnail, nullptr, nullptr);

    if (ret != SDK::CrError_None) {
        char hex_buf[32];
        snprintf(hex_buf, sizeof(hex_buf), "0x%08X", static_cast<unsigned int>(ret));
        result.error_message = std::string("Failed to start thumbnail download: ") + hex_buf;
        m_getContentsDataStartFlg = false;
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
        m_pendingTransfers.push_back(
            {effectiveSaveDir, std::move(preSnapshot), std::chrono::steady_clock::now(),
             static_cast<unsigned int>(content_id), static_cast<unsigned int>(file_id)});
    }
    m_inFlightContentId.store(static_cast<unsigned int>(content_id));
    m_inFlightFileId.store(static_cast<unsigned int>(file_id));
    {
    }
    startTransferPolling();

    result.success = true;
    result.message = "Thumbnail download started. Listen for transferProgress SSE events for completion.";
    return result;
}

CameraDeviceRest::FileDownloadResult CameraDeviceRest::download_remote_transfer_screennail(
    int slot_number, CrInt32u content_id, CrInt32u file_id, const std::string& save_path) {
    FileDownloadResult result;
    result.success = false;
    result.progress_percent = 0;

    SDK::CrSlotNumber slotNumber;
    if (slot_number == 1) slotNumber = SDK::CrSlotNumber_Slot1;
    else if (slot_number == 2) slotNumber = SDK::CrSlotNumber_Slot2;
    else { result.error_message = "Invalid slot number. Use 1 or 2."; return result; }

    if (!save_path.empty()) set_save_info(save_path, "", -1, nullptr);

    m_getContentsDataStartFlg = true;
    std::string effectiveSaveDir = save_path.empty() ? m_savePath : save_path;
    auto preSnapshot = snapshotImageFiles(effectiveSaveDir);

    SDK::CrError ret = SDK::GetRemoteTransferContentsCompressedDataFile(
        m_deviceHandle, slotNumber, content_id, file_id,
        SDK::CrGetContentsCompressedDataType_Screennail, nullptr, nullptr);

    if (ret != SDK::CrError_None) {
        char hex_buf[32];
        snprintf(hex_buf, sizeof(hex_buf), "0x%08X", static_cast<unsigned int>(ret));
        result.error_message = std::string("Failed to start screennail download: ") + hex_buf;
        m_getContentsDataStartFlg = false;
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
        m_pendingTransfers.push_back(
            {effectiveSaveDir, std::move(preSnapshot), std::chrono::steady_clock::now(),
             static_cast<unsigned int>(content_id), static_cast<unsigned int>(file_id)});
    }
    m_inFlightContentId.store(static_cast<unsigned int>(content_id));
    m_inFlightFileId.store(static_cast<unsigned int>(file_id));
    {
    }
    startTransferPolling();

    result.success = true;
    result.message = "Screennail download started. Listen for transferProgress SSE events for completion.";
    return result;
}

namespace {

// REST button name -> CrCameraButtonFunction. The SDK value packs this code in
// the upper 16 bits and an up/down value in the lower 16.
const std::pair<const char*, CrInt32u> kCameraButtons[] = {
    {"up",                        SDK::CrCameraButtonFunction_UpButton},
    {"down",                      SDK::CrCameraButtonFunction_DownButton},
    {"left",                      SDK::CrCameraButtonFunction_LeftButton},
    {"right",                     SDK::CrCameraButtonFunction_RightButton},
    {"enter",                     SDK::CrCameraButtonFunction_EnterButton},
    {"menu",                      SDK::CrCameraButtonFunction_MenuButton},
    {"multi-selector-up",         SDK::CrCameraButtonFunction_MultiSelectorUp},
    {"multi-selector-down",       SDK::CrCameraButtonFunction_MultiSelectorDown},
    {"multi-selector-left",       SDK::CrCameraButtonFunction_MultiSelectorLeft},
    {"multi-selector-right",      SDK::CrCameraButtonFunction_MultiSelectorRight},
    {"multi-selector-enter",      SDK::CrCameraButtonFunction_MultiSelectorEnter},
    {"multi-selector-right-up",   SDK::CrCameraButtonFunction_MultiSelectorRightUp},
    {"multi-selector-right-down", SDK::CrCameraButtonFunction_MultiSelectorRightDown},
    {"multi-selector-left-up",    SDK::CrCameraButtonFunction_MultiSelectorLeftUp},
    {"multi-selector-left-down",  SDK::CrCameraButtonFunction_MultiSelectorLeftDown},
    {"fn",                        SDK::CrCameraButtonFunction_FnButton},
    {"playback",                  SDK::CrCameraButtonFunction_PlaybackButton},
    {"delete",                    SDK::CrCameraButtonFunction_DeleteButton},
    {"mode",                      SDK::CrCameraButtonFunction_ModeButton},
    {"c1",                        SDK::CrCameraButtonFunction_C1Button},
    {"c2",                        SDK::CrCameraButtonFunction_C2Button},
    {"c3",                        SDK::CrCameraButtonFunction_C3Button},
    {"c4",                        SDK::CrCameraButtonFunction_C4Button},
    {"c5",                        SDK::CrCameraButtonFunction_C5Button},
    {"c6",                        SDK::CrCameraButtonFunction_C6Button},
    {"c7",                        SDK::CrCameraButtonFunction_C7Button},
    {"movie",                     SDK::CrCameraButtonFunction_MovieButton},
    {"ael",                       SDK::CrCameraButtonFunction_AELButton},
    {"af-on",                     SDK::CrCameraButtonFunction_AFOnButton},
    {"home",                      SDK::CrCameraButtonFunction_HomeButton},
    {"clips",                     SDK::CrCameraButtonFunction_ClipsButton},
    {"slot-select",               SDK::CrCameraButtonFunction_SlotSelectButton},
    {"display",                   SDK::CrCameraButtonFunction_DisplayButton},
    {"cancel-back",               SDK::CrCameraButtonFunction_CancelBackButton},
    {"thumbnail",                 SDK::CrCameraButtonFunction_ThumbnailButton},
};

bool lookupCameraButton(const std::string& name, CrInt32u& code) {
    for (const auto& b : kCameraButtons) {
        if (name == b.first) { code = b.second; return true; }
    }
    return false;
}

const char* focusFrameTypeName(int type) {
    switch (type) {
        case SDK::CrFocusFrameType_PhaseDetection_AFSensor:    return "phase-detection-af-sensor";
        case SDK::CrFocusFrameType_PhaseDetection_ImageSensor: return "phase-detection-image-sensor";
        case SDK::CrFocusFrameType_Wide:                       return "wide";
        case SDK::CrFocusFrameType_Zone:                       return "zone";
        case SDK::CrFocusFrameType_CentralEmphasis:            return "central-emphasis";
        case SDK::CrFocusFrameType_ContrastFlexibleMain:       return "contrast-flexible-main";
        case SDK::CrFocusFrameType_ContrastFlexibleAssist:     return "contrast-flexible-assist";
        case SDK::CrFocusFrameType_Contrast:                   return "contrast";
        case SDK::CrFocusFrameType_FrameSomewhere:             return "frame-somewhere";
        default:                                               return "unknown";
    }
}

const char* trackingFrameTypeName(int type) {
    switch (type) {
        case SDK::CrTrackingFrameType_NonTargetAF: return "non-target-af";
        case SDK::CrTrackingFrameType_TargetAF:    return "target-af";
        default:                                   return "unknown";
    }
}

const char* focusFrameStateName(int state) {
    switch (state) {
        case SDK::CrFocusFrameState_NotFocused:          return "not-focused";
        case SDK::CrFocusFrameState_Focused:             return "focused";
        case SDK::CrFocusFrameState_FocusFrameSelection: return "selection";
        case SDK::CrFocusFrameState_Moving:              return "moving";
        case SDK::CrFocusFrameState_RegistrationAF:      return "registration-af";
        case SDK::CrFocusFrameState_Island:              return "island";
        default:                                         return "unknown";
    }
}

}  // namespace

std::vector<std::string> CameraDeviceRest::known_camera_buttons() {
    std::vector<std::string> names;
    names.reserve(std::size(kCameraButtons));
    for (const auto& b : kCameraButtons) names.emplace_back(b.first);
    return names;
}

std::vector<std::string> CameraDeviceRest::supported_camera_buttons() {
    std::vector<std::string> names;

    CrInt32 nprop = 0;
    SDK::CrDeviceProperty* list = nullptr;
    CrInt32u code = SDK::CrDeviceProperty_CameraButtonFunction;
    if (CR_FAILED(SDK::GetSelectDeviceProperties(m_deviceHandle, 1, &code, &list, &nprop)) ||
        nprop < 1 || list == nullptr) {
        if (list) SDK::ReleaseDeviceProperties(m_deviceHandle, list);
        return names;
    }

    // Reading the property yields the set of keys this body can drive; the
    // values carry the button code in their upper 16 bits.
    CrInt32u size = list[0].GetValueSize();
    auto* values = reinterpret_cast<CrInt32u*>(list[0].GetValues());
    if (values && size >= sizeof(CrInt32u)) {
        CrInt32u count = size / static_cast<CrInt32u>(sizeof(CrInt32u));
        for (CrInt32u i = 0; i < count; ++i) {
            CrInt32u buttonCode = values[i] & 0xFFFF0000u;
            for (const auto& b : kCameraButtons) {
                if (b.second == buttonCode) {
                    if (std::find(names.begin(), names.end(), b.first) == names.end())
                        names.emplace_back(b.first);
                    break;
                }
            }
        }
    }
    SDK::ReleaseDeviceProperties(m_deviceHandle, list);
    return names;
}

bool CameraDeviceRest::press_camera_button(const std::string& button,
                                           const std::string& action,
                                           std::string* errorDetail) {
    CrInt32u buttonCode = 0;
    if (!lookupCameraButton(button, buttonCode)) {
        if (errorDetail) *errorDetail = "Unknown button: '" + button + "'";
        return false;
    }

    // The SDK refuses to START a button press while a key is already held, so
    // surface that rather than failing opaquely.
    //
    // A release must never be gated on this: after our own "down" the camera
    // correctly reports AnyKeyOn, and blocking the matching "up" would strand
    // the key in the held state with no way to let go.
    if (action != "up") {
        CrInt64u status = 0;
        if (fetchOne(m_deviceHandle, SDK::CrDeviceProperty_CameraButtonFunctionStatus, status)) {
            if (status != SDK::CrCameraButtonFunctionStatus_Idle) {
                if (errorDetail)
                    *errorDetail = "Camera is not idle — a key is already held. "
                                   "Release it first with {\"action\":\"up\"}.";
                return false;
            }
        }
    }

    auto write = [&](CrInt32u value) {
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDeviceProperty_CameraButtonFunction);
        prop.SetCurrentValue(buttonCode | value);
        prop.SetValueType(SDK::CrDataType_UInt32);
        return !CR_FAILED(SDK::SetDeviceProperty(m_deviceHandle, &prop));
    };

    // Up = 0x0001, Down = 0x0002 — the reverse of CrCommandParam.
    const CrInt32u kDown = SDK::CrCameraButtonFunctionValue_Down;
    const CrInt32u kUp   = SDK::CrCameraButtonFunctionValue_Up;

    if (action == "down") {
        if (!write(kDown)) { if (errorDetail) *errorDetail = "Camera rejected the button-down"; return false; }
        return true;
    }
    if (action == "up") {
        if (!write(kUp)) { if (errorDetail) *errorDetail = "Camera rejected the button-up"; return false; }
        return true;
    }

    // Full press. Release even if the press failed, so a partial failure cannot
    // leave the camera believing a key is still held down.
    bool downOk = write(kDown);
    std::this_thread::sleep_for(100ms);
    bool upOk = write(kUp);
    if (!downOk || !upOk) {
        if (errorDetail)
            *errorDetail = downOk ? "Button press sent but release failed"
                                  : "Camera rejected the button press";
        return false;
    }
    return true;
}

CameraDeviceRest::AFAreaPositionResult CameraDeviceRest::get_af_area_position() {
    AFAreaPositionResult result;

    SDK::CrLiveViewProperty* props = nullptr;
    CrInt32 numProps = 0;
    CrInt32u code = SDK::CrLiveViewPropertyCode::CrLiveViewProperty_AF_Area_Position;

    SDK::CrError err =
        SDK::GetSelectLiveViewProperties(m_deviceHandle, 1, &code, &props, &numProps);
    if (CR_FAILED(err) || props == nullptr || numProps <= 0) {
        char hex[32];
        snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(err));
        result.error = std::string("GetSelectLiveViewProperties failed: ") + hex;
        if (props) SDK::ReleaseLiveViewProperties(m_deviceHandle, props);
        return result;
    }

    for (CrInt32 i = 0; i < numProps; ++i) {
        if (props[i].GetCode() != SDK::CrLiveViewProperty_AF_Area_Position) continue;
        if (!props[i].IsGetEnableCurrentValue()) {
            result.error = "Camera reports the AF area frame as not currently readable";
            break;
        }

        auto* raw = props[i].GetValue();
        CrInt32u size = props[i].GetValueSize();
        if (raw == nullptr || size < sizeof(SDK::CrFocusFrameInfo)) {
            result.error = "AF area frame payload was empty";
            break;
        }

        // The payload holds an array — a tracking or expanded area reports more
        // than one frame, and a UI generally wants to draw all of them.
        result.available = true;
        CrInt32u count = size / static_cast<CrInt32u>(sizeof(SDK::CrFocusFrameInfo));
        auto* info = reinterpret_cast<SDK::CrFocusFrameInfo*>(raw);
        for (CrInt32u f = 0; f < count; ++f) {
            AFFrame frame;
            frame.type         = static_cast<int>(info[f].type);
            frame.state        = static_cast<int>(info[f].state);
            frame.typeName     = focusFrameTypeName(frame.type);
            frame.stateName    = focusFrameStateName(frame.state);
            frame.priority     = info[f].priority;
            frame.xNumerator   = info[f].xNumerator;
            frame.xDenominator = info[f].xDenominator;
            frame.yNumerator   = info[f].yNumerator;
            frame.yDenominator = info[f].yDenominator;
            frame.width        = info[f].width;
            frame.height       = info[f].height;
            result.frames.push_back(frame);
        }
        break;
    }

    SDK::ReleaseLiveViewProperties(m_deviceHandle, props);
    return result;
}

bool CameraDeviceRest::set_af_area_position(unsigned int x, unsigned int y,
                                            std::string* errorDetail) {
    // Documented coordinate space: X 0-639, Y 0-479. The usable area is smaller
    // (inset by half the frame size) and varies by model, aspect and AF setting,
    // so the camera may clamp or ignore a value inside these bounds.
    if (x > 639 || y > 479) {
        if (errorDetail)
            *errorDetail = "Out of range: x must be 0-639 and y must be 0-479";
        return false;
    }

    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_AF_Area_Position);
    prop.SetCurrentValue((static_cast<CrInt32u>(x) << 16) | static_cast<CrInt32u>(y));
    prop.SetValueType(SDK::CrDataType_UInt32Range);

    SDK::CrError err = SDK::SetDeviceProperty(m_deviceHandle, &prop);
    if (CR_FAILED(err)) {
        if (errorDetail) {
            char hex[32];
            snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(err));
            *errorDetail = std::string("SetDeviceProperty failed: ") + hex;
        }
        return false;
    }
    return true;
}

CameraDeviceRest::TrackingFrameResult CameraDeviceRest::get_tracking_frame() {
    TrackingFrameResult result;

    SDK::CrLiveViewProperty* props = nullptr;
    CrInt32 numProps = 0;
    CrInt32u code = SDK::CrLiveViewPropertyCode::CrLiveViewProperty_TrackingFrame;

    SDK::CrError err =
        SDK::GetSelectLiveViewProperties(m_deviceHandle, 1, &code, &props, &numProps);
    if (CR_FAILED(err) || props == nullptr || numProps <= 0) {
        char hex[32];
        snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(err));
        result.error = std::string("GetSelectLiveViewProperties failed: ") + hex;
        if (props) SDK::ReleaseLiveViewProperties(m_deviceHandle, props);
        return result;
    }

    for (CrInt32 i = 0; i < numProps; ++i) {
        if (props[i].GetCode() != SDK::CrLiveViewProperty_TrackingFrame) continue;
        if (!props[i].IsGetEnableCurrentValue()) {
            result.error = "Camera reports the tracking frame as not currently readable";
            break;
        }
        // The code is shared with other frame payloads, so trust the tag rather
        // than the size: a face or focus frame here would decode into nonsense.
        if (props[i].GetFrameInfoType() != SDK::CrFrameInfoType_TrackingFrameInfo) {
            result.error = "Camera returned a different frame type for the tracking frame";
            break;
        }

        // Readable but empty is the normal state when nothing is being tracked,
        // so that is a successful read with no frames rather than an error.
        result.available = true;

        auto* raw = props[i].GetValue();
        CrInt32u size = props[i].GetValueSize();
        if (raw == nullptr || size < sizeof(SDK::CrTrackingFrameInfo)) break;

        CrInt32u count = size / static_cast<CrInt32u>(sizeof(SDK::CrTrackingFrameInfo));
        auto* info = reinterpret_cast<SDK::CrTrackingFrameInfo*>(raw);
        for (CrInt32u f = 0; f < count; ++f) {
            TrackingFrame frame;
            frame.type         = static_cast<int>(info[f].type);
            frame.state        = static_cast<int>(info[f].state);
            frame.typeName     = trackingFrameTypeName(frame.type);
            frame.stateName    = focusFrameStateName(frame.state);
            frame.priority     = info[f].priority;
            frame.xNumerator   = info[f].xNumerator;
            frame.xDenominator = info[f].xDenominator;
            frame.yNumerator   = info[f].yNumerator;
            frame.yDenominator = info[f].yDenominator;
            frame.width        = info[f].width;
            frame.height       = info[f].height;
            result.frames.push_back(frame);
        }
        break;
    }

    SDK::ReleaseLiveViewProperties(m_deviceHandle, props);
    return result;
}

bool CameraDeviceRest::is_remote_touch_supported() {
    CrInt64u status = 0;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_RemoteTouchOperationEnableStatus, status))
        return false;
    return status == SDK::CrRemoteTouchOperation_Enable;
}

bool CameraDeviceRest::is_cancel_remote_touch_supported() {
    CrInt64u status = 0;
    if (!fetchOne(m_deviceHandle,
                  SDK::CrDeviceProperty_CancelRemoteTouchOperationEnableStatus, status))
        return false;
    return status == SDK::CrCancelRemoteTouchOperation_Enable;
}

bool CameraDeviceRest::remote_touch(unsigned int x, unsigned int y,
                                    std::string* errorDetail) {
    // Same coordinate space as the AF area position: X 0-639, Y 0-479.
    if (x > 639 || y > 479) {
        if (errorDetail)
            *errorDetail = "Out of range: x must be 0-639 and y must be 0-479";
        return false;
    }

    // The camera rejects the write outright when touch is not currently
    // available, so report the gate rather than an opaque SDK error. On some
    // bodies (ILCE-7SM3, ILCE-7C) touch is movie-mode only, which shows up here.
    if (!is_remote_touch_supported()) {
        if (errorDetail)
            *errorDetail = "Camera reports remote touch as unavailable right now "
                           "(RemoteTouchOperationEnableStatus is Disable)";
        return false;
    }

    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_RemoteTouchOperation);
    prop.SetCurrentValue((static_cast<CrInt32u>(x) << 16) | static_cast<CrInt32u>(y));
    prop.SetValueType(SDK::CrDataType_UInt32);

    SDK::CrError err = SDK::SetDeviceProperty(m_deviceHandle, &prop);
    if (CR_FAILED(err)) {
        if (errorDetail) {
            char hex[32];
            snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(err));
            *errorDetail = std::string("SetDeviceProperty failed: ") + hex;
        }
        return false;
    }
    return true;
}

bool CameraDeviceRest::cancel_remote_touch(std::string* errorDetail) {
    // Cancel is only accepted while something is actually being tracked, which
    // is exactly what its own enable status reports.
    if (!is_cancel_remote_touch_supported()) {
        if (errorDetail)
            *errorDetail = "Nothing to cancel — the camera reports "
                           "CancelRemoteTouchOperationEnableStatus as Disable";
        return false;
    }

    // Down then Up, matching how the SDK sample drives the other cancel
    // commands; the operation runs on the Up.
    SDK::SendCommand(m_deviceHandle, SDK::CrCommandId_CancelRemoteTouchOperation,
                     SDK::CrCommandParam_Down);
    SDK::CrError err = SDK::SendCommand(m_deviceHandle,
                                        SDK::CrCommandId_CancelRemoteTouchOperation,
                                        SDK::CrCommandParam_Up);
    if (CR_FAILED(err)) {
        if (errorDetail) {
            char hex[32];
            snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(err));
            *errorDetail = std::string("SendCommand failed: ") + hex;
        }
        return false;
    }
    return true;
}

std::map<std::uint64_t, std::string> CameraDeviceRest::getDisplayStringNames(
    SDK::CrDisplayStringType type, int timeoutMs) {
    // Cache hit — return immediately. Without this, every getAllProperties call
    // on bodies whose display-string callback never fires waits the full
    // timeoutMs, freezing the bulk-properties endpoint on each call. These names
    // are static for the session; a LUT import would invalidate them but those
    // are rare and a server restart picks it up.
    {
        std::lock_guard<std::mutex> lk(m_dispNameCacheMutex);
        auto it = m_dispNameCache.find(type);
        if (it != m_dispNameCache.end()) return it->second;
    }

    std::map<std::uint64_t, std::string> result;

    SDK::CrError err = SDK::RequestDisplayStringList(m_deviceHandle, type);
    if (CR_FAILED(err)) return result;

    {
        std::unique_lock<std::mutex> lock(m_dispCameraKeyMutex);
        m_dispCameraKeyCV.wait_for(lock, std::chrono::milliseconds(timeoutMs));
    }

    SDK::CrDisplayStringListInfo* dispList = nullptr;
    CrInt32u dispCount = 0;
    err = SDK::GetDisplayStringList(m_deviceHandle, type, &dispList, &dispCount);
    if (CR_SUCCEEDED(err) && dispCount > 0 && dispList) {
        for (CrInt32u i = 0; i < dispCount; ++i) {
            std::string name;
            for (CrInt32u c = 0; c < dispList[i].displayStringSize; ++c) {
                char ch = static_cast<char>(dispList[i].displayString[c]);
                if (ch == '\0') break;
                name += ch;
            }
            result[dispList[i].value] = name;
        }
        SDK::ReleaseDisplayStringList(m_deviceHandle, dispList);
    }

    // Cache regardless of result — an empty map still means "we asked and the
    // SDK didn't deliver", and re-asking won't change that within a session.
    {
        std::lock_guard<std::mutex> lk(m_dispNameCacheMutex);
        m_dispNameCache[type] = result;
    }

    return result;
}

void CameraDeviceRest::release_contents_info(int slotIndex) {
    if (slotIndex < 0 || slotIndex > 1) return;
    if (m_captureDateList[slotIndex]) {
        SDK::ReleaseRemoteTransferCapturedDateList(m_deviceHandle, m_captureDateList[slotIndex]);
        m_captureDateList[slotIndex] = nullptr;
    }
    if (m_contentsInfoList[slotIndex]) {
        SDK::ReleaseRemoteTransferContentsInfoList(m_deviceHandle, m_contentsInfoList[slotIndex]);
        m_contentsInfoList[slotIndex] = nullptr;
    }
}

// --- Save-destination info ----------------------------------------------
bool CameraDeviceRest::set_save_info(const std::string& path, const std::string& prefix,
                                     int startNo, std::string* errorDetail) {
    m_savePath = path;
    m_savePrefix = prefix;
    m_saveStartNo = startNo;
    auto status = SDK::SetSaveInfo(m_deviceHandle,
                                   const_cast<char*>(path.c_str()),
                                   const_cast<char*>(prefix.c_str()),
                                   startNo);
    if (CR_FAILED(status)) {
        if (errorDetail) {
            char hex[32];
            snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned int>(status));
            *errorDetail = hex;
        }
        return false;
    }
    return true;
}

// --- Camera settings file save/load -------------------------------------
bool CameraDeviceRest::is_settings_save_supported() {
    CrInt64u readState = 0, saveOp = 0;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_CameraSetting_SaveRead_State, readState))
        return false;
    if (readState == SDK::CrCameraSettingSaveReadState_Reading) return false;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_CameraSetting_SaveOperationEnableStatus, saveOp))
        return false;
    return saveOp == SDK::CrCameraSettingSaveOperation_Enable;
}

bool CameraDeviceRest::is_settings_load_supported() {
    CrInt64u readState = 0, readOp = 0;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_CameraSetting_SaveRead_State, readState))
        return false;
    if (readState == SDK::CrCameraSettingSaveReadState_Reading) return false;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_CameraSetting_ReadOperationEnableStatus, readOp))
        return false;
    return readOp == SDK::CrCameraSettingReadOperation_Enable;
}

SCRSDK::CrError CameraDeviceRest::download_camera_settings(const std::string& filepath,
                                                          const std::string& filename) {
    if (!is_settings_save_supported()) return SDK::CrError_Generic;
    return SDK::DownloadSettingFile(m_deviceHandle,
                                    SDK::CrDownloadSettingFileType_Setup,
                                    (CrChar*)filepath.c_str(),
                                    (CrChar*)filename.c_str());
}

SCRSDK::CrError CameraDeviceRest::upload_camera_settings(const std::string& filepath) {
    if (!is_settings_load_supported()) return SDK::CrError_Generic;
    return SDK::UploadSettingFile(m_deviceHandle,
                                  SDK::CrUploadSettingFileType_Setup,
                                  (CrChar*)filepath.c_str());
}

// --- Zoom ----------------------------------------------------------------
bool CameraDeviceRest::is_zoom_operation_supported() {
    CrInt64u status = 0;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_Zoom_Operation_Status, status))
        return false;
    return status == SDK::CrZoomOperationEnableStatus_Enable;
}

SCRSDK::CrError CameraDeviceRest::execute_zoom_operation_direct(int8_t speed) {
    if (!is_zoom_operation_supported()) return SDK::CrError_Generic;
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDeviceProperty_Zoom_Operation);
    prop.SetCurrentValue((CrInt64u)speed);
    prop.SetValueType(SDK::CrDataType_UInt16Array);
    return SDK::SetDeviceProperty(m_deviceHandle, &prop);
}

uint32_t CameraDeviceRest::get_zoom_distance_mm() {
    CrInt64u distance = 0;
    if (!fetchOne(m_deviceHandle, SDK::CrDeviceProperty_ZoomDistance, distance))
        return 0;
    // zoom_distance is in 0.001mm units; convert to mm.
    return static_cast<uint32_t>(distance / 1000);
}

// --- Property-change callback correlation --------------------------------
void CameraDeviceRest::registerPropertyWait(CrInt32u propertyCode,
                                            std::promise<bool>* promise) {
    std::lock_guard<std::mutex> lock(m_propertyCallbackMutex);
    m_propertyCallbackCode = propertyCode;
    m_propertyCallbackPromise = promise;
}

void CameraDeviceRest::clearPropertyWait() {
    std::lock_guard<std::mutex> lock(m_propertyCallbackMutex);
    m_propertyCallbackCode = 0;
    m_propertyCallbackPromise = nullptr;
}

// --- SCRSDK::IDeviceCallback --------------------------------------------
void CameraDeviceRest::OnConnected(SCRSDK::DeviceConnectionVersioin /*version*/) {
    m_connected.store(true);
    if (m_eventCallback) {
        cli::text model(get_model());   // width-aware (Windows Cr_Core returns UTF-16)
        cli::text id(get_id());
        m_eventCallback("connected",
            "{\"model\":\"" + jsonEscape(std::string(model.data())) + "\",\"id\":\"" +
            jsonEscape(std::string(id.data())) + "\"}");
    }
}

void CameraDeviceRest::OnDisconnected(CrInt32u /*error*/) {
    m_connected.store(false);
    if (m_eventCallback) m_eventCallback("disconnected", "{}");
}

void CameraDeviceRest::OnPropertyChanged() {}
void CameraDeviceRest::OnLvPropertyChanged() {}

void CameraDeviceRest::OnCompleteDownload(CrChar* filename, CrInt32u type) {
    if (m_eventCallback) {
        cli::text file(filename);
        std::string fileType =
            (type == SCRSDK::CrDownloadSettingFileType_Setup) ? "settings" : "image";
        // filename is a full host path on auto-transfer (C:\Users\... on Windows),
        // so it must be escaped or the event body is unparseable JSON.
        m_eventCallback("downloadComplete",
            "{\"filename\":\"" + jsonEscape(std::string(file.data())) + "\",\"type\":\"" +
            fileType + "\"}");
    }
}

void CameraDeviceRest::OnWarning(CrInt32u warning) {
    // Wake getDisplayStringNames() as soon as the SDK reports the requested
    // display-string list is ready (or failed) instead of waiting the timeout.
    if (warning == SDK::CrWarning_RequestDisplayStringList_Success ||
        warning == SDK::CrWarning_RequestDisplayStringList_Error) {
        m_dispCameraKeyCV.notify_all();
    }

    if (!m_eventCallback) return;

    std::string eventType;
    std::string eventData;

    switch (warning) {
    case SDK::CrWarning_Connect_Reconnecting:
        eventType = "reconnecting";
        eventData = "{}";
        break;
    case SDK::CrWarning_Connect_Reconnected:
        eventType = "reconnected";
        eventData = "{}";
        break;
    case SDK::CrWarning_Format_Complete:
        eventType = "formatResult";
        eventData = R"({"success":true})";
        break;
    case SDK::CrWarning_Format_Failed:
    case SDK::CrWarning_Format_Invalid:
        eventType = "formatResult";
        eventData = R"({"success":false})";
        break;
    case SDK::CrWarning_Format_Canceled:
        eventType = "formatResult";
        eventData = R"({"success":false,"canceled":true})";
        break;
    case SDK::CrWarning_FocusPosition_Result_OK:
        eventType = "focusResult";
        eventData = R"({"success":true})";
        break;
    case SDK::CrWarning_FocusPosition_Result_NG:
    case SDK::CrWarning_FocusPosition_Result_Invalid:
        eventType = "focusResult";
        eventData = R"({"success":false})";
        break;
    case SDK::CrWarning_CameraSettings_Read_Result_OK:
        eventType = "settingsResult";
        eventData = R"({"operation":"read","success":true})";
        break;
    case SDK::CrWarning_CameraSettings_Read_Result_NG:
        eventType = "settingsResult";
        eventData = R"({"operation":"read","success":false})";
        break;
    case SDK::CrWarning_CameraSettings_Save_Result_NG:
        eventType = "settingsResult";
        eventData = R"({"operation":"save","success":false})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_OK:
        eventType = "lutImportResult";
        eventData = R"({"success":true})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_NG:
        eventType = "lutImportResult";
        eventData = R"({"success":false,"error":"general_failure"})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_InvalidFileName:
        eventType = "lutImportResult";
        eventData = R"({"success":false,"error":"invalid_filename"})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_DeviceBusy:
        eventType = "lutImportResult";
        eventData = R"({"success":false,"error":"device_busy"})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_DeviceStorageFull:
        eventType = "lutImportResult";
        eventData = R"({"success":false,"error":"storage_full"})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_InvalidParameter:
        eventType = "lutImportResult";
        eventData = R"({"success":false,"error":"invalid_parameter"})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_InvalidFile:
        eventType = "lutImportResult";
        eventData = R"({"success":false,"error":"invalid_file"})";
        break;
    case SDK::CrWarning_ImportLUTFile_Result_Invalid:
        eventType = "lutImportResult";
        eventData = R"({"success":false,"error":"invalid"})";
        break;
    case SDK::CrWarning_CautionDisplay:
        eventType = "cautionDisplay";
        eventData = R"({"message":"Camera caution display active"})";
        break;
    default:
        // The SDK sometimes routes CrWarningExt codes (>= 0x60000) through
        // OnWarning, where the AF state params are not available.
        if (warning == SDK::CrWarningExt_AFStatus) {
            eventType = "afStatus";
            eventData = R"({"state":"unlocked","source":"OnWarning"})";
        } else {
            std::ostringstream oss;
            oss << R"({"code":"0x)" << std::hex << warning << std::dec
                << R"(","message":")" << CrErrorString(warning).c_str() << R"("})";
            eventType = "warning";
            eventData = oss.str();
        }
        break;
    }

    if (!eventType.empty()) m_eventCallback(eventType, eventData);
}

void CameraDeviceRest::OnWarningExt(CrInt32u warning, CrInt32 param1, CrInt32 /*param2*/,
                                    CrInt32 /*param3*/) {
    if (!m_eventCallback) return;
    if (warning != SDK::CrWarningExt_AFStatus) return;

    // Unlike OnWarning, this variant carries the AF state in param1.
    std::string state;
    switch (param1) {
    case SDK::CrWarningExt_AFStatusParam_Focused_AF_S:
    case SDK::CrWarningExt_AFStatusParam_Focused_AF_C:
        state = "focused";
        break;
    case SDK::CrWarningExt_AFStatusParam_NotFocused_AF_S:
    case SDK::CrWarningExt_AFStatusParam_NotFocused_AF_C:
        state = "unlocked";
        break;
    default:
        state = "tracking";
        break;
    }
    m_eventCallback("afStatus", "{\"state\":\"" + state + "\",\"source\":\"OnWarningExt\"}");
}
void CameraDeviceRest::OnError(CrInt32u error) {
    // Where an authentication rejection lands. Recording it is what lets a
    // caller tell a refused password from a connection that simply has not
    // finished yet.
    m_lastError.store(error);
}

void CameraDeviceRest::OnPropertyChangedCodes(CrInt32u num, CrInt32u* codes) {
    // Correlate a pending property-set wait.
    {
        std::lock_guard<std::mutex> lock(m_propertyCallbackMutex);
        if (m_propertyCallbackCode) {
            for (CrInt32u i = 0; i < num; ++i) {
                if (codes[i] == m_propertyCallbackCode) {
                    m_propertyCallbackCode = 0;
                    if (m_propertyCallbackPromise) {
                        m_propertyCallbackPromise->set_value(true);
                        m_propertyCallbackPromise = nullptr;
                    }
                    break;
                }
            }
        }
    }
    // Forward to SSE (filter noisy status properties).
    if (m_eventCallback) {
        static const std::set<CrInt32u> filtered = {0x799};  // TimeShiftShootingStatus
        std::vector<CrInt32u> keep;
        for (CrInt32u i = 0; i < num; ++i) {
            if (filtered.find(codes[i]) == filtered.end()) keep.push_back(codes[i]);
        }
        if (!keep.empty()) {
            std::ostringstream oss;
            oss << "{\"codes\":[";
            for (size_t i = 0; i < keep.size(); ++i) {
                if (i > 0) oss << ",";
                oss << "\"0x" << std::hex << keep[i] << "\"";
            }
            oss << std::dec << "],\"count\":" << keep.size() << "}";
            m_eventCallback("propertyChanged", oss.str());
        }
    }
}

void CameraDeviceRest::OnLvPropertyChangedCodes(CrInt32u /*num*/, CrInt32u* /*codes*/) {}

void CameraDeviceRest::OnNotifyContentsTransfer(CrInt32u notify,
                                               SCRSDK::CrContentHandle handle,
                                               CrChar* filename) {
    if (!m_eventCallback) return;
    std::string status = (notify == SDK::CrNotify_ContentsTransfer_Start) ? "started"
                       : (notify == SDK::CrNotify_ContentsTransfer_Complete) ? "complete"
                       : "error";
    std::ostringstream oss;
    oss << "{\"status\":\"" << status << "\",\"handle\":\"0x" << std::hex << handle
        << std::dec << "\"";
    if (filename) {
        cli::text file(filename);
        bool valid = !file.empty();
        for (auto c : file) {
            if (c < 0x20 || c > 0x7E) { valid = false; break; }
        }
        if (valid) {
            oss << ",\"filename\":\"" << jsonEscape(std::string(file.data())) << "\"";
        }
    }
    oss << "}";
    m_eventCallback("contentsTransfer", oss.str());
}

// --- Property loading / transfer polling stubs ---------------------------
// The generic property path in CameraWebController reads/writes properties
// directly via get_device_handle(); this layer fetches individual properties on
// demand (fetchOne), so a monolithic loader is unnecessary.
void CameraDeviceRest::load_properties(CrInt32u /*num*/, CrInt32u* /*codes*/) {}

// --- Remote-transfer result callbacks ------------------------------------
void CameraDeviceRest::OnNotifyRemoteTransferResult(CrInt32u notify, CrInt32u per,
                                                    CrChar* filename) {
    // The SDK reports progress itself on this build, so the disk-polling
    // fallback must stay out of the way from now on.
    m_realTransferCallbackSeen.store(true);
    // A real SDK callback fired — cancel the disk-polling fallback. Capture the
    // pending entry's identifiers first: the SDK hands us only a filename, and
    // the spec's transferProgress carries contentId/fileId.
    const unsigned int contentId = m_inFlightContentId.load();
    const unsigned int fileId = m_inFlightFileId.load();
    {
        std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
        m_pendingTransfers.clear();
    }
    if (m_eventCallback) {
        std::ostringstream oss;
        oss << "{\"percent\":" << per << ",\"notify\":\"0x" << std::hex << notify << std::dec << "\""
            << ",\"cameraId\":\"" << jsonEscape(std::string(get_id().data())) << "\""
            << ",\"contentId\":" << contentId
            << ",\"fileId\":" << fileId;
        if (filename) {
            cli::text file(filename);
            const std::string path = jsonEscape(std::string(file.data()));
            // `savedPath` is the documented field; `filename` is retained
            // because existing consumers (mcp/src/tools/files.ts) read it.
            oss << ",\"savedPath\":\"" << path << "\""
                << ",\"filename\":\"" << path << "\"";
        }
        oss << "}";
        m_eventCallback("transferProgress", oss.str());
    }
}

void CameraDeviceRest::OnNotifyRemoteTransferResult(CrInt32u /*notify*/, CrInt32u /*per*/,
                                                    CrInt8u* /*data*/, CrInt64u /*size*/) {}

void CameraDeviceRest::OnNotifyRemoteTransferContentsListChanged(CrInt32u /*notify*/,
                                                                 CrInt32u /*slotNumber*/,
                                                                 CrInt32u /*addSize*/) {}

// --- Transfer-completion disk polling (macOS V2.01 callback-miss fallback) --
void CameraDeviceRest::startTransferPolling() {
    // Never race the SDK once it has proven it reports transfers itself.
    if (m_realTransferCallbackSeen.load()) return;
    bool expected = false;
    if (!m_transferPollRunning.compare_exchange_strong(expected, true)) return;
    m_transferPollThread = std::thread(&CameraDeviceRest::transferPollLoop, this);
}

void CameraDeviceRest::stopTransferPolling() {
    m_transferPollRunning = false;
    if (m_transferPollThread.joinable()) m_transferPollThread.join();
}

void CameraDeviceRest::transferPollLoop() {
    // Number of consecutive polls a candidate file's size must stay unchanged
    // before we treat the transfer as finished (500ms per tick).
    constexpr int kStableTicksRequired = 3;

    while (m_transferPollRunning) {
        std::this_thread::sleep_for(500ms);
        // The SDK started reporting for itself mid-flight — drop the fallback
        // rather than emit a competing (and likely premature) completion.
        if (m_realTransferCallbackSeen.load()) {
            std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
            m_pendingTransfers.clear();
            continue;
        }
        std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
        auto now = std::chrono::steady_clock::now();
        for (auto it = m_pendingTransfers.begin(); it != m_pendingTransfers.end();) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->startTime).count();
            if (elapsed > 120) { it = m_pendingTransfers.erase(it); continue; }

            if (it->candidate.empty()) {
                auto currentFiles = snapshotImageFiles(it->saveDir);
                for (auto& f : currentFiles) {
                    if (it->preSnapshot.find(f) == it->preSnapshot.end()) {
                        it->candidate = f;
                        break;
                    }
                }
                if (it->candidate.empty()) { ++it; continue; }
            }

            // The file exists but the SDK may still be streaming into it. Only
            // report completion once its size has settled.
            std::error_code ec;
            auto path = fs::path(it->saveDir) / it->candidate;
            auto size = fs::file_size(path, ec);
            if (ec) { ++it; continue; }

            if (size == it->lastSize && size > 0) {
                ++it->stableTicks;
            } else {
                it->lastSize = size;
                it->stableTicks = 0;
            }

            if (it->stableTicks < kStableTicksRequired) { ++it; continue; }

            if (m_eventCallback) {
                std::ostringstream oss;
                // path.string() is a native host path — C:\Users\... on Windows.
                const std::string saved = jsonEscape(path.string());
                oss << "{\"percent\":100,\"notify\":\"0x20093\""
                    << ",\"cameraId\":\"" << jsonEscape(std::string(get_id().data())) << "\""
                    << ",\"contentId\":" << it->contentId
                    << ",\"fileId\":" << it->fileId
                    << ",\"savedPath\":\"" << saved << "\""
                    << ",\"filename\":\"" << saved << "\""
                    << ",\"synthetic\":true}";
                m_eventCallback("transferProgress", oss.str());
            }
            it = m_pendingTransfers.erase(it);
        }
    }
}

}  // namespace rest
