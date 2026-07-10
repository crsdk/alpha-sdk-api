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
// SSH-authenticated connections require a pre-negotiated fingerprint/password;
// the interactive stdin prompts from the SDK sample are intentionally omitted.
// Local USB/remote connections (the common REST case) use empty credentials.
bool CameraDeviceRest::connect(SCRSDK::CrSdkControlMode openMode,
                               SCRSDK::CrReconnectingSet reconnect) {
    m_modeSDK = openMode;
    const char* inputId = "admin";
    auto status = SDK::Connect(m_info, this, &m_deviceHandle, openMode, reconnect,
                               inputId, "", "", 0);
    if (CR_FAILED(status)) {
        return false;
    }
    // Default save destination to the current working directory.
    set_save_info(".", "", kImageSaveAutoStartNo, nullptr);
    return true;
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

cli::text CameraDeviceRest::get_id() const {
    if (cli::ConnectionType::NETWORK == m_connType) {
        return m_info->GetMACAddressChar();
    }
    return cli::text((char*)m_info->GetId());
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
            {effectiveSaveDir, std::move(preSnapshot), std::chrono::steady_clock::now()});
    }
    startTransferPolling();

    result.success = true;
    result.message = "Download started (remote-transfer)";
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
        cli::text model(m_info->GetModel());
        cli::text id(get_id());
        m_eventCallback("connected",
            "{\"model\":\"" + std::string(model.data()) + "\",\"id\":\"" +
            std::string(id.data()) + "\"}");
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
        m_eventCallback("downloadComplete",
            "{\"filename\":\"" + std::string(file.data()) + "\",\"type\":\"" +
            fileType + "\"}");
    }
}

void CameraDeviceRest::OnWarning(CrInt32u /*warning*/) {}
void CameraDeviceRest::OnError(CrInt32u /*error*/) {}

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
            std::string escaped;
            for (char c : std::string(file.data())) {
                if (c == '"' || c == '\\') escaped += '\\';
                escaped += c;
            }
            oss << ",\"filename\":\"" << escaped << "\"";
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
    // A real SDK callback fired — cancel the disk-polling fallback.
    {
        std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
        m_pendingTransfers.clear();
    }
    if (m_eventCallback) {
        std::ostringstream oss;
        oss << "{\"percent\":" << per << ",\"notify\":\"0x" << std::hex << notify << std::dec << "\"";
        if (filename) {
            cli::text file(filename);
            oss << ",\"filename\":\"" << file.data() << "\"";
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
    bool expected = false;
    if (!m_transferPollRunning.compare_exchange_strong(expected, true)) return;
    m_transferPollThread = std::thread(&CameraDeviceRest::transferPollLoop, this);
}

void CameraDeviceRest::stopTransferPolling() {
    m_transferPollRunning = false;
    if (m_transferPollThread.joinable()) m_transferPollThread.join();
}

void CameraDeviceRest::transferPollLoop() {
    while (m_transferPollRunning) {
        std::this_thread::sleep_for(500ms);
        std::lock_guard<std::mutex> lock(m_pendingTransfersMtx);
        auto now = std::chrono::steady_clock::now();
        for (auto it = m_pendingTransfers.begin(); it != m_pendingTransfers.end();) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->startTime).count();
            if (elapsed > 120) { it = m_pendingTransfers.erase(it); continue; }
            auto currentFiles = snapshotImageFiles(it->saveDir);
            std::string newFile;
            for (auto& f : currentFiles) {
                if (it->preSnapshot.find(f) == it->preSnapshot.end()) { newFile = f; break; }
            }
            if (!newFile.empty()) {
                if (m_eventCallback) {
                    std::ostringstream oss;
                    oss << "{\"percent\":100,\"notify\":\"0x20093\",\"filename\":\""
                        << newFile << "\",\"synthetic\":true}";
                    m_eventCallback("transferProgress", oss.str());
                }
                it = m_pendingTransfers.erase(it);
            } else {
                ++it;
            }
        }
    }
}

}  // namespace rest
