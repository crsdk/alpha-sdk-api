#ifndef CAMERA_DEVICE_REST_H
#define CAMERA_DEVICE_REST_H

// CameraDeviceRest — MIT-licensed, non-interactive Sony camera device layer.
//
// This class is the REST server's own device abstraction. It replaces the
// heavily-forked `cli::CameraDevice` that used to live in shared/core. The
// public repo ships only the *stock* Sony SDK sample sources in shared/core
// (fetched from the SDK download, never redistributed); all REST-specific,
// non-interactive behavior lives here in api/server as original MIT code.
//
// Design notes:
//   * The generic property get/set path in CameraWebController talks to the SDK
//     directly using get_device_handle() (SDK::GetSelectDeviceProperties /
//     SDK::SetDeviceProperty). So the primary contract this class exposes is the
//     device handle plus the connection/callback lifecycle. Higher-level flows
//     that the SDK models as multi-step (camera-settings transfer, zoom, movie
//     toggle, AF+shutter) are provided as explicit methods.
//   * It owns all of its own state — it does NOT reach into stock CameraDevice
//     private members. It implements SCRSDK::IDeviceCallback itself so it can
//     forward SDK events to SSE and correlate property-change callbacks with
//     bounded HTTP waits.
//   * Stock helper classes from shared/core (PropertyValueTable, CrDebugString,
//     OpenCVWrapper) remain reusable and are called from here.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <functional>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "CameraRemote_SDK.h"
#include "IDeviceCallback.h"
#include "ConnectionInfo.h"      // stock: ConnectionType
#include "PropertyValueTable.h"  // stock: value parse/format helpers
#include "Text.h"                // stock: text type

namespace rest {

// Forward SDK events to external consumers (SSE / WebSocket).
// Parameters: eventType, jsonData.
using SdkEventCallback =
    std::function<void(const std::string& eventType, const std::string& jsonData)>;

// Tracks a pending file download for the macOS SDK V2.01 polling fallback.
// Kept separate from normal callback handling so a future SDK version can drop
// it without changing the transfer API.
struct PendingTransfer {
    std::string saveDir;                              // directory to poll
    std::set<std::string> preSnapshot;                // files present before download
    std::chrono::steady_clock::time_point startTime;
    // The SDK creates the destination file when the transfer STARTS and streams
    // into it, so "a new file appeared" does not mean "the transfer finished".
    // Completion is only declared once the size stops changing between polls.
    std::string   candidate;                          // new file seen, still growing
    std::uintmax_t lastSize = 0;
    int            stableTicks = 0;
};

class CameraDeviceRest : public SCRSDK::IDeviceCallback {
public:
    CameraDeviceRest() = delete;
    CameraDeviceRest(std::int32_t no, SCRSDK::ICrCameraObjectInfo const* camera_info);
    ~CameraDeviceRest();

    // --- SSE / event forwarding ---
    void setEventCallback(SdkEventCallback cb) { m_eventCallback = std::move(cb); }

    // --- Connection lifecycle (non-interactive) ---
    // Credentials are optional and only apply to bodies with access
    // authentication switched on. Leave them empty for USB and for networked
    // bodies with authentication off — that path is unchanged.
    bool connect(SCRSDK::CrSdkControlMode openMode, SCRSDK::CrReconnectingSet reconnect,
                 const std::string& userId = "", const std::string& password = "",
                 const std::string& fingerprint = "");
    bool disconnect();
    bool release();
    bool is_connected() const { return m_connected.load(); }

    // SDK::Connect is asynchronous: it returns as soon as the request has been
    // accepted, and the real outcome arrives later on OnConnected or OnError.
    // Anything that reports success to a caller has to wait for that, otherwise
    // a rejected password looks identical to a good one.
    // Returns true once connected, false on error or timeout.
    bool wait_for_connection(int timeoutMs);
    unsigned int last_error() const { return m_lastError.load(); }

    // Does this body use SSH / access authentication at all?
    bool ssh_supported() const;
    // The camera's own fingerprint, straight from the SDK. Sony's sample fetches
    // this and asks the user to confirm it rather than having them transcribe it
    // off the camera screen, which suggests the SDK's rendering is the only one
    // Connect accepts. Empty on failure.
    std::string get_fingerprint();

    // --- Identity / handle (primary contract used by the controller) ---
    std::int32_t get_number() const { return m_number; }
    std::int64_t get_device_handle() const { return m_deviceHandle; }
    cli::text get_model() const;   // width-aware decode (see .cpp) — Windows Cr_Core returns UTF-16
    cli::text get_id() const;      // width-aware decode (see .cpp)
    SCRSDK::CrSdkControlMode get_sdkmode() const { return m_modeSDK; }

    // --- Property helpers (formatting; raw get/set is done SDK-direct by the
    //     controller via get_device_handle()) ---
    void get_property(SCRSDK::CrDeviceProperty& prop) const;   // single-property fetch
    cli::text getCurrentStr(SCRSDK::CrDeviceProperty* prop);   // format current value

    // --- Exposure / focus / drive setters ---
    // The dedicated legacy endpoints for ISO/aperture/shutter/WB/focus/exposure
    // are superseded by the generic property endpoint (handle-direct
    // SetDeviceProperty in CameraWebController). The stock SDK sample implemented
    // these *interactively* (stdin prompts), which is unusable server-side, so
    // they are intentionally no-ops here.
    // Legacy no-arg worker setters (ISO/aperture/shutter/WB): the REST endpoints
    // set these via the generic property path; these overloads are only reached
    // by the legacy CameraOperationWorker and are intentionally inert.
    void set_iso() {}
    void set_aperture() {}
    void set_shutter_speed() {}
    void set_white_balance() {}

    // Parameterized, non-interactive setters that REST endpoints call directly.
    bool set_exposure_program_mode(CrInt64u value);
    bool set_focus_mode(CrInt64u value);
    bool set_focus_area(CrInt64u value);
    bool set_drive_mode(CrInt64u value);
    bool set_priority_key_to_pc_remote();  // enable PC remote control

    // --- Shooting actions (non-interactive) ---
    void capture_image() const;
    void shutter_down() const;
    void shutter_up() const;
    void s1_shooting() const;                    // half-press (delegates to non-interactive)
    void s1_shooting_non_interactive() const;    // half-press down, wait, up
    bool af_shutter() const;                    // false if camera is in MF mode
    bool toggle_movie_recording_direct();       // non-interactive movie rec toggle

    // --- Contents / remote-transfer file browsing -------------------------
    // Used only in ContentsTransfer / RemoteTransfer connection modes.
    struct MtpFileEntry {
        uint32_t handle;
        std::string fileName;
        uint64_t fileSize;
        std::string date;   // "YYYYMMDDTHHMMSS"
        uint32_t width;
        uint32_t height;
    };
    struct MtpContentsListResult {
        bool success = false;
        std::string error_message;
        std::vector<MtpFileEntry> files;
    };
    struct ContentsListResult {
        bool success = false;
        std::string error_message;
        std::vector<SCRSDK::CrContentsInfo> contents;
    };
    struct FileDownloadResult {
        bool success = false;
        std::string error_message;
        std::string message;
        std::string filename;
        int progress_percent = 0;
    };

    MtpContentsListResult list_contents_transfer_files();
    ContentsListResult list_remote_transfer_contents(int slot_number);
    FileDownloadResult download_contents_transfer_file(CrInt32u content_handle,
                                                       const std::string& save_path);
    FileDownloadResult download_remote_transfer_file(int slot_number, CrInt32u content_id,
                                                     CrInt32u file_id,
                                                     const std::string& save_path);
    // Compressed preview downloads (Remote Transfer mode): thumbnail (~50-60 KB)
    // for fast ML triage, screennail (~250-300 KB) for detailed evaluation.
    FileDownloadResult download_remote_transfer_thumbnail(int slot_number, CrInt32u content_id,
                                                          CrInt32u file_id,
                                                          const std::string& save_path);
    FileDownloadResult download_remote_transfer_screennail(int slot_number, CrInt32u content_id,
                                                           CrInt32u file_id,
                                                           const std::string& save_path);

    // --- Camera button presses --------------------------------------------
    // Presses a physical button on the body (menu, enter, C1-C7, delete, …) by
    // writing CrDeviceProperty_CameraButtonFunction: the button occupies the
    // upper 16 bits and up/down the lower 16.
    //
    // Note the up/down values are the reverse of CrCommandParam — here
    // Up = 0x0001 and Down = 0x0002.
    //
    // `action` is "press" (or empty) for a full down-then-up, or "down"/"up" to
    // hold and release separately.
    bool press_camera_button(const std::string& button, const std::string& action,
                             std::string* errorDetail = nullptr);

    // Every button name this layer understands, whether or not a given body
    // supports it.
    static std::vector<std::string> known_camera_buttons();

    // The subset the connected camera reports as operable. Empty means the
    // camera did not report the property at all.
    std::vector<std::string> supported_camera_buttons();

    // --- AF area position -------------------------------------------------
    // One focus frame as the camera reports it. Position and size are
    // fractions: xNumerator/xDenominator is the frame centre across the live
    // view, so a UI can draw the box without knowing the sensor resolution.
    struct AFFrame {
        int          type = 0;        // CrFocusFrameType
        int          state = 0;       // CrFocusFrameState
        // Resolved here rather than in the HTTP layer, which holds no SDK types.
        std::string  typeName;
        std::string  stateName;
        unsigned int priority = 0;
        unsigned int xNumerator = 0, xDenominator = 0;
        unsigned int yNumerator = 0, yDenominator = 0;
        unsigned int width = 0, height = 0;
    };
    struct AFAreaPositionResult {
        bool                  available = false;  // camera reported the property
        std::vector<AFFrame>  frames;             // may hold more than one frame
        std::string           error;
    };

    // Reading the AF frame is NOT symmetric with writing it. GetDeviceProperties
    // reports CrDeviceProperty_AF_Area_Position but its value is always 0 — the
    // live-view property is the only source of the actual position, and
    // SetDeviceProperty is the only way to move it.
    AFAreaPositionResult get_af_area_position();
    // x is packed into the upper 16 bits and y into the lower 16.
    bool set_af_area_position(unsigned int x, unsigned int y, std::string* errorDetail = nullptr);

    // --- Remote touch (tracking AF) ---------------------------------------
    // Not the same thing as moving the AF box. set_af_area_position() drags a
    // frame to a point and leaves it there; a remote touch seeds the camera's
    // subject tracking, which then follows the subject on its own. It also works
    // in focus areas that have no movable box, where set_af_area_position() has
    // nothing to move.
    //
    // The camera reports the resulting box through its own live-view property
    // rather than the AF area frame, so it gets its own struct — the payload is
    // a CrTrackingFrameInfo, whose type field is a CrTrackingFrameType and not
    // the CrFocusFrameType the AF frame carries.
    struct TrackingFrame {
        int          type = 0;        // CrTrackingFrameType
        int          state = 0;       // CrFocusFrameState, shared with the AF frame
        // Resolved here rather than in the HTTP layer, which holds no SDK types.
        std::string  typeName;
        std::string  stateName;
        unsigned int priority = 0;
        unsigned int xNumerator = 0, xDenominator = 0;
        unsigned int yNumerator = 0, yDenominator = 0;
        unsigned int width = 0, height = 0;
    };
    struct TrackingFrameResult {
        bool                       available = false;  // property was readable
        std::vector<TrackingFrame> frames;             // empty when nothing is tracked
        std::string                error;
    };

    // The live-view property code aliases CrDeviceProperty_RemoteTouchOperation,
    // so reading the touch property is what reports the tracking box.
    TrackingFrameResult get_tracking_frame();

    // Enable-status gates, the same idiom as LUT import and the settings files.
    bool is_remote_touch_supported();
    bool is_cancel_remote_touch_supported();

    // Same packing as the AF area position: x in the upper 16 bits, y in the
    // lower 16, X 0-639 and Y 0-479.
    bool remote_touch(unsigned int x, unsigned int y, std::string* errorDetail = nullptr);
    bool cancel_remote_touch(std::string* errorDetail = nullptr);

    // --- Camera-supplied display strings (LUT / base-look names) ---
    // Maps an SDK value to the camera's own display name for `type`. Results are
    // cached per session: bodies that never fire the display-string callback would
    // otherwise stall every bulk-properties call for the full timeout.
    std::map<std::uint64_t, std::string> getDisplayStringNames(SCRSDK::CrDisplayStringType type,
                                                               int timeoutMs = 3000);

    // --- Save-destination info (Remote Transfer mode) ---
    bool set_save_info(const std::string& path, const std::string& prefix, int startNo,
                       std::string* errorDetail = nullptr);
    std::string get_save_info_path() const { return m_savePath; }
    std::string get_save_info_prefix() const { return m_savePrefix; }
    int get_save_info_start_no() const { return m_saveStartNo; }

    // --- Camera settings file save/load ---
    SCRSDK::CrError download_camera_settings(const std::string& filepath,
                                             const std::string& filename);
    SCRSDK::CrError upload_camera_settings(const std::string& filepath);
    bool is_settings_save_supported();
    bool is_settings_load_supported();

    // --- Zoom ---
    bool is_zoom_operation_supported();
    SCRSDK::CrError execute_zoom_operation_direct(int8_t speed);  // -10..+10, 0=stop
    uint32_t get_zoom_distance_mm();

    // --- SCRSDK::IDeviceCallback (forward to SSE + correlate property changes) ---
    void OnConnected(SCRSDK::DeviceConnectionVersioin version) override;
    void OnDisconnected(CrInt32u error) override;
    void OnPropertyChanged() override;
    void OnLvPropertyChanged() override;
    void OnCompleteDownload(CrChar* filename, CrInt32u type) override;
    void OnWarning(CrInt32u warning) override;
    void OnWarningExt(CrInt32u warning, CrInt32 param1, CrInt32 param2,
                      CrInt32 param3) override;
    void OnError(CrInt32u error) override;
    void OnPropertyChangedCodes(CrInt32u num, CrInt32u* codes) override;
    void OnLvPropertyChangedCodes(CrInt32u num, CrInt32u* codes) override;
    void OnNotifyContentsTransfer(CrInt32u notify, SCRSDK::CrContentHandle handle,
                                  CrChar* filename) override;
    void OnNotifyRemoteTransferResult(CrInt32u notify, CrInt32u per, CrChar* filename) override;
    void OnNotifyRemoteTransferResult(CrInt32u notify, CrInt32u per, CrInt8u* data,
                                      CrInt64u size) override;
    void OnNotifyRemoteTransferContentsListChanged(CrInt32u notify, CrInt32u slotNumber,
                                                   CrInt32u addSize) override;

    // --- Property-change callback correlation for bounded HTTP waits ---
    // Register before the SDK set call; the OnPropertyChanged* callbacks fulfill
    // the promise; caller unregisters on success/timeout/error.
    void registerPropertyWait(CrInt32u propertyCode, std::promise<bool>* promise);
    void clearPropertyWait();

private:
    // Internal SDK wrappers (replace stock CameraDevice private helpers).
    void load_properties(CrInt32u num = 0, CrInt32u* codes = nullptr);
    bool set_property(SCRSDK::CrDeviceProperty& prop) const;

    // Release per-slot remote-transfer content lists.
    void release_contents_info(int slotIndex);

    // macOS V2.01 transfer-completion polling fallback.
    void startTransferPolling();
    void stopTransferPolling();
    void transferPollLoop();

    // Identity / connection
    std::int32_t                 m_number;
    SCRSDK::ICrCameraObjectInfo* m_info;
    std::int64_t                 m_deviceHandle{0};
    std::atomic<bool>            m_connected{false};
    // Last error reported by OnError; 0 when none. Cleared on each connect.
    std::atomic<unsigned int>    m_lastError{0};
    cli::ConnectionType          m_connType{};
    SCRSDK::CrSdkControlMode     m_modeSDK{};

    // Stock helper for parsing/formatting property values.
    cli::PropertyValueTable      m_prop;

    // SSE forwarding
    SdkEventCallback             m_eventCallback;

    // Property-change callback correlation
    std::mutex                   m_propertyCallbackMutex;
    CrInt32u                     m_propertyCallbackCode{0};
    std::promise<bool>*          m_propertyCallbackPromise{nullptr};

    // Save-destination info
    std::string                  m_savePath;
    std::string                  m_savePrefix;
    int                          m_saveStartNo{1};

    // Transfer polling workaround state
    std::mutex                   m_pendingTransfersMtx;
    std::vector<PendingTransfer> m_pendingTransfers;
    std::thread                  m_transferPollThread;
    std::atomic<bool>            m_transferPollRunning{false};
    // Set once the SDK's own OnNotifyRemoteTransferResult is seen. The polling
    // fallback exists only for builds where that callback never fires; once it
    // has fired we must never emit synthetic completions again, or clients get
    // a premature percent:100 while the file is still being written.
    std::atomic<bool>            m_realTransferCallbackSeen{false};

    // Remote-transfer per-slot content lists (SDK-allocated; freed via
    // release_contents_info) and the download-in-progress flag the transfer
    // callbacks read.
    SCRSDK::CrContentsInfo* m_contentsInfoList[2] = {nullptr, nullptr};
    SCRSDK::CrCaptureDate*  m_captureDateList[2]  = {nullptr, nullptr};
    bool                    m_getContentsDataStartFlg = false;

    // Display-string list state. m_dispCameraKeyCV is signalled from OnWarning
    // when the SDK reports RequestDisplayStringList success/error; the cache
    // keeps bodies that never fire that callback from stalling every call.
    std::mutex                                                    m_dispCameraKeyMutex;
    std::condition_variable                                       m_dispCameraKeyCV;
    std::mutex                                                    m_dispNameCacheMutex;
    std::map<SCRSDK::CrDisplayStringType,
             std::map<std::uint64_t, std::string>>                m_dispNameCache;
};

}  // namespace rest

#endif  // CAMERA_DEVICE_REST_H
