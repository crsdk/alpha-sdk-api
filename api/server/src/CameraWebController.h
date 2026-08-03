#ifndef CAMERAWEBCONTROLLER_H
#define CAMERAWEBCONTROLLER_H

// On Windows, process <windows.h> (and thus <winnt.h>) before any project
// header. Sony's stock Text.h does `#define TCHAR char` (char-based, no UNICODE);
// if <winnt.h> is included afterwards, its `typedef char TCHAR` becomes
// `typedef char char` and fails to compile. WIN32_LEAN_AND_MEAN also keeps
// winsock1 out so CameraWebServer's <winsock2.h> stays valid.
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <queue>
#include <condition_variable>
#include <future>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>
#include "device/CameraDeviceRest.h"
#include "device/RestPropertyParsers.h"
#include "CameraRemote_SDK.h"

namespace cli {

// The camera device layer is the MIT-licensed rest::CameraDeviceRest (in
// api/server/src/device/). The stock Sony sample's cli::CameraDevice is no
// longer part of this repository. This alias keeps the controller's existing
// `CameraDevice` references working while routing them to the REST device layer.
using CameraDevice = ::rest::CameraDeviceRest;

// Enum for connection modes matching SDK
enum class ConnectionMode {
    Remote = 0,
    ContentsTransfer = 1,
    RemoteTransfer = 2
};

// Structure to hold camera information for JSON responses
struct CameraInfo {
    std::string model;
    std::string id;
    std::string connectionType;
    bool connected;

    CameraInfo() : connected(false), connectionType("USB") {}
};

// Structure for API response
struct ApiResponse {
    bool success;
    std::string message;
    CameraInfo camera;
    std::unordered_map<std::string, std::string> data;  // Generic data field for flexible responses

    ApiResponse() : success(false) {}
};

// Callback correlation tracking structure
struct CallbackListener {
    std::string correlationId;
    std::string cameraId;
    std::string propertyName;
    CrInt32u propertyCode;
    std::promise<ApiResponse> promise;
    std::chrono::steady_clock::time_point startTime;

    CallbackListener(const std::string& corrId, const std::string& camId,
                     const std::string& propName, CrInt32u propCode)
        : correlationId(corrId), cameraId(camId), propertyName(propName),
          propertyCode(propCode), startTime(std::chrono::steady_clock::now()) {}
};

// Structure for available property values
struct AvailablePropertyValue {
    CrInt64u value;
    std::string hexValue;
    std::string formatted;
};

// Structure for property data (used by helper functions)
struct PropertyData {
    std::string propertyName;
    CrInt64u currentValue;
    std::string currentHexValue;
    std::string currentFormatted;
    bool writable;
    std::vector<AvailablePropertyValue> availableValues;

    PropertyData() : currentValue(0), writable(false) {}
};

// RESTful API Mapping Structures
// Property mapping: REST property name -> SDK property code and method info
struct PropertyMapping {
    std::string restName;           // REST API name (e.g., "iso", "aperture")
    CrInt32u sdkPropertyCode;       // SDK property code (e.g., CrDeviceProperty_IsoSensitivity)
    std::string sdkMethodName;      // CameraDevice method name (e.g., "set_iso", "get_iso")
    bool hasCustomMethod;           // true if CameraDevice has a specific method for this property

    PropertyMapping() : sdkPropertyCode(0), hasCustomMethod(false) {}
    PropertyMapping(const std::string& rest, CrInt32u code, const std::string& method, bool custom)
        : restName(rest), sdkPropertyCode(code), sdkMethodName(method), hasCustomMethod(custom) {}
};

// Action mapping: REST action name -> SDK command info
struct ActionMapping {
    std::string restName;           // REST API name (e.g., "shutter", "half-press")
    CrInt32u sdkCommandId;          // SDK command ID (e.g., CrCommandId_Release)
    CrInt32u commandParam;          // SDK command parameter (e.g., CrCommandParam_Down)
    std::string sdkMethodName;      // CameraDevice method name (e.g., "capture_image", "s1_shooting")
    bool hasCustomMethod;           // true if CameraDevice has a specific method for this action

    ActionMapping() : sdkCommandId(0), commandParam(0), hasCustomMethod(false) {}
    ActionMapping(const std::string& rest, CrInt32u cmdId, CrInt32u param, const std::string& method, bool custom)
        : restName(rest), sdkCommandId(cmdId), commandParam(param), sdkMethodName(method), hasCustomMethod(custom) {}
};

// Forward declarations for worker classes
class CameraOperationWorker;
class LiveViewOSDWorker;

// Command types for camera operations
enum class CameraCommandType {
    Capture,
    SetISO,
    SetAperture,
    SetShutterSpeed,
    SetWhiteBalance,
    StartLiveView,
    StopLiveView,
    AutoFocus,
    ZoomIn,
    ZoomOut,
    GetCurrentISO,
    GetCurrentAperture,
    GetCurrentShutterSpeed,
    GetCurrentWhiteBalance,
    GetCurrentFocusMode,
    GetAllCurrentSettings,
    Connect,
    Disconnect
};

// Command structure for inter-thread communication
struct CameraCommand {
    CameraCommandType type;
    std::string parameter;  // For commands that need parameters (e.g., setISO value)
    std::promise<ApiResponse> responsePromise;

    CameraCommand(CameraCommandType t, const std::string& param = "")
        : type(t), parameter(param) {}
};

// Camera thread pair management
struct CameraThreadPair {
    std::shared_ptr<CameraDevice> camera;
    std::unique_ptr<CameraOperationWorker> operationWorker;
    std::unique_ptr<LiveViewOSDWorker> liveViewWorker;
    std::thread operationThread;
    std::thread liveViewThread;
    std::atomic<bool> active;

    CameraThreadPair() : active(false) {}
};

// Callback for forwarding camera events to the web server (SSE broadcasting)
// Parameters: cameraId, eventType, jsonData
using WebEventCallback = std::function<void(const std::string& cameraId,
                                             const std::string& eventType,
                                             const std::string& jsonData)>;

class CameraWebController {
public:
    CameraWebController();
    ~CameraWebController();

    // Set callback for forwarding SDK events to the web server layer (SSE)
    void setEventCallback(WebEventCallback callback) { m_webEventCallback = std::move(callback); }

    // Main API methods that will be called from web server
    ApiResponse getStatus();
    ApiResponse connectCamera(const std::string& connectionMode = "remote-transfer", const std::string& cameraId = "", const std::string& username = "", const std::string& password = "", const std::string& reconnecting = "off");
    ApiResponse disconnectCamera();
    ApiResponse disconnectCamera(const std::string& cameraId);  // Per-camera overload
    ApiResponse getStatus(const std::string& cameraId);          // Per-camera overload
    std::vector<CameraInfo> getAvailableCameras();
    
    // Camera control methods
    ApiResponse captureImage();
    ApiResponse setISO(const std::string& cameraId, const std::string& isoValue);
    ApiResponse setAperture(const std::string& cameraId, const std::string& apertureValue);
    ApiResponse setShutterSpeed(const std::string& cameraId, const std::string& shutterValue);
    ApiResponse setWhiteBalance(const std::string& cameraId, const std::string& wbValue);
    ApiResponse startLiveView();
    ApiResponse stopLiveView();
    ApiResponse autoFocus();
    ApiResponse zoomIn();
    ApiResponse zoomOut();

    // Camera property retrieval methods
    ApiResponse getCurrentISO(const std::string& cameraId);
    ApiResponse getCurrentAperture(const std::string& cameraId);
    ApiResponse getCurrentShutterSpeed(const std::string& cameraId);
    ApiResponse getCurrentWhiteBalance(const std::string& cameraId);
    ApiResponse getCurrentFocusMode(const std::string& cameraId);
    ApiResponse getAllCurrentSettings();

    // Bulk property retrieval for UI initialization
    ApiResponse getAllProperties(const std::string& cameraId);

    // Action methods
    ApiResponse s1Shooting(const std::string& cameraId);  // Half-press
    ApiResponse afShutter(const std::string& cameraId);    // AF + capture
    ApiResponse setExposureMode(const std::string& cameraId, const std::string& mode);
    ApiResponse setDriveMode(const std::string& cameraId, const std::string& mode);
    ApiResponse setFocusMode(const std::string& cameraId, const std::string& mode);
    ApiResponse setFocusArea(const std::string& cameraId, const std::string& area);
    ApiResponse toggleMovieRecording(const std::string& cameraId);

    // Priority key settings (REQUIRED for remote control)
    ApiResponse getPriorityKey(const std::string& cameraId);
    ApiResponse setPriorityKey(const std::string& cameraId, const std::string& setting);

    // Save destination settings (for Remote Transfer mode)
    ApiResponse getSaveDestination(const std::string& cameraId);
    ApiResponse setSaveDestination(const std::string& cameraId, const std::string& path, const std::string& prefix, int startNo);

    // Camera settings file save/load operations
    ApiResponse downloadCameraSettings(const std::string& cameraId, const std::string& filename);
    ApiResponse uploadCameraSettings(const std::string& cameraId, const std::string& filename);
    ApiResponse listSavedSettings();

    // LUT import
    ApiResponse importLUT(const std::string& cameraId, const std::string& filePath, int slotNumber);

    // Zoom control operations
    ApiResponse executeZoomAction(const std::string& cameraId, int speed);
    ApiResponse getZoomDistance(const std::string& cameraId);

    // Image ID (MakerNote) handlers
    ApiResponse getImageIdString(const std::string& cameraId);
    ApiResponse setImageIdString(const std::string& cameraId, const std::string& value);
    ApiResponse getImageIdNum(const std::string& cameraId);
    ApiResponse setImageIdNum(const std::string& cameraId, const std::string& value);

    // RESTful Generic Handlers (Scalable to 800+ properties/actions)
    ApiResponse getPropertyGeneric(const std::string& cameraId, const std::string& propertyName);
    ApiResponse setPropertyGeneric(const std::string& cameraId, const std::string& propertyName, const std::string& value);
    ApiResponse executeActionGeneric(const std::string& cameraId, const std::string& actionName, const std::string& params = "");

    // Helper: Convert string value to SDK numeric value (supports string enums and hex)
    CrInt64u convertValueToSDK(const std::string& value, const std::string& propertyName);

    // Helper: Format available values for a property (returns JSON array string)
    std::string formatAvailableValuesJSON(const std::string& propertyName,
                                          const SCRSDK::CrDeviceProperty& prop,
                                          std::shared_ptr<CameraDevice> camera);

    // Helper: Get full property data from camera (used by both custom and generic getters)
    PropertyData getPropertyData(std::shared_ptr<CameraDevice> camera,
                                const std::string& propertyName,
                                CrInt32u propertyCode);

    // Overload: Extract property data from an already-fetched CrDeviceProperty (no SDK call)
    PropertyData getPropertyDataFromBulk(std::shared_ptr<CameraDevice> camera,
                                         const std::string& propertyName,
                                         const SCRSDK::CrDeviceProperty& property);

    // Helper: Format property data (shared by getPropertyData and getPropertyDataFromBulk)
    void formatPropertyData(PropertyData& data, std::shared_ptr<CameraDevice> camera,
                           const std::string& propertyName,
                           const unsigned char* values, int valueSize);

    // Helper: Build ApiResponse from PropertyData
    ApiResponse buildPropertyResponse(const PropertyData& propertyData,
                                     std::shared_ptr<CameraDevice> camera);

    // Live view streaming methods (legacy - single camera)
    ApiResponse startLiveViewStreaming();
    ApiResponse stopLiveViewStreaming();
    bool getLiveViewFrame(std::vector<uint8_t>& jpegData);
    bool isLiveViewStreaming() const { return m_liveViewStreaming.load(); }

    // Per-camera live view enable/disable (SDK device setting)
    ApiResponse enableLiveView(const std::string& cameraId);
    ApiResponse disableLiveView(const std::string& cameraId);
    ApiResponse getLiveViewStatus(const std::string& cameraId);

    // Per-camera live view streaming
    ApiResponse startLiveViewStreaming(const std::string& cameraId);
    ApiResponse stopLiveViewStreaming(const std::string& cameraId);
    bool getLiveViewFrame(const std::string& cameraId, std::vector<uint8_t>& jpegData);

    // OSD Image overlay methods (legacy - single camera)
    ApiResponse enableOSDMode();
    ApiResponse disableOSDMode();
    ApiResponse getOSDStatus();
    bool getCompositeFrame(std::vector<uint8_t>& jpegData);

    // Per-camera OSD methods
    ApiResponse enableOSD(const std::string& cameraId);
    ApiResponse disableOSD(const std::string& cameraId);
    ApiResponse getOSDStatus(const std::string& cameraId);
    bool getCompositeFrame(const std::string& cameraId, std::vector<uint8_t>& jpegData);
    
    // Utility methods
    std::string toJson(const ApiResponse& response);
    std::string toJson(const std::vector<CameraInfo>& cameras);

    // Camera device access for SD card operations
    std::shared_ptr<CameraDevice> getCameraDevice(const std::string& cameraId);

    // Get connection mode for a camera (for dispatch logic in SD card, etc.)
    ConnectionMode getConnectionMode(const std::string& cameraId);

    // SDK initialization state
    bool isSDKInitialized() const { return m_sdkInitialized; }

    // Disconnect state access for worker threads
    bool isDisconnectInProgress() const { return m_disconnectInProgress.load(); }
    
private:
    std::mutex m_discoveryMutex;      // Guards m_availableCameras, m_currentConnectionMode, m_currentCamera
    std::mutex m_sdkLifecycleMutex;   // Guards SDK::Init / SDK::Release only
    std::shared_ptr<CameraDevice> m_currentCamera; // Legacy support - will be removed
    std::vector<std::shared_ptr<CameraDevice>> m_availableCameras;
    ConnectionMode m_currentConnectionMode;

    // New multi-camera thread management
    std::map<std::string, std::unique_ptr<CameraThreadPair>> m_cameraThreads;
    std::map<std::string, ConnectionMode> m_connectedModes;  // Per-camera connection mode tracking
    std::mutex m_cameraThreadsMutex;

    // Legacy live view streaming (will be moved to worker threads)
    std::atomic<bool> m_liveViewStreaming;
    std::thread m_liveViewThread;
    std::vector<uint8_t> m_latestFrame;
    std::mutex m_frameMutex;

    // Legacy OSD Image overlay (will be moved to worker threads)
    std::atomic<bool> m_osdEnabled;
    std::vector<uint8_t> m_latestCompositeFrame;
    std::mutex m_compositeMutex;

    // Helper methods
    void discoverCameras();
    bool initializeSDK();
    void releaseSDK();
    void liveViewStreamingThread(); // Legacy - will be removed

    // New worker thread management methods
    void createCameraThreads(const std::string& cameraId, std::shared_ptr<CameraDevice> camera);
    void destroyCameraThreads(const std::string& cameraId);
    ApiResponse executeCameraCommand(const std::string& cameraId, CameraCommandType commandType, const std::string& parameter = "");

    bool m_sdkInitialized;

    // Disconnect state tracking to prevent SDK calls during shutdown
    std::atomic<bool> m_disconnectInProgress;


    // Callback correlation system
    std::unordered_map<std::string, std::unique_ptr<CallbackListener>> m_callbackListeners;
    std::mutex m_callbackMutex;

    // Callback correlation helper methods
    std::string generateCorrelationId();
    void registerCallbackListener(const std::string& cameraId, const std::string& propertyName,
                                 const std::string& correlationId, CrInt32u propertyCode,
                                 std::promise<ApiResponse>&& promise);
    void unregisterCallbackListener(const std::string& correlationId);
    bool handlePropertyCallback(const std::string& cameraId, CrInt32u propertyCode);

    // Property code mapping helpers
    CrInt32u getPropertyCode(const std::string& propertyName) const;
    std::string getPropertyName(CrInt32u propertyCode) const;
    std::string getCurrentPropertyValue(const std::string& cameraId, CrInt32u propertyCode);

    // SSE event callback
    WebEventCallback m_webEventCallback;

    // Wire event callback to a CameraDevice instance
    void wireCameraEventCallback(std::shared_ptr<CameraDevice> camera, const std::string& cameraId);
};

// Camera Operation Worker Thread Class
class CameraOperationWorker {
public:
    CameraOperationWorker(std::shared_ptr<CameraDevice> camera, const std::string& cameraId);
    ~CameraOperationWorker();

    void start();
    void stop();
    std::future<ApiResponse> executeCommand(CameraCommandType commandType, const std::string& parameter = "");
    void workerLoop(); // Made public for std::thread

private:
    std::shared_ptr<CameraDevice> m_camera;
    std::string m_cameraId;
    std::atomic<bool> m_running;

    // Command queue
    std::queue<std::unique_ptr<CameraCommand>> m_commandQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;

    ApiResponse processCommand(const CameraCommand& command);

    // Individual command handlers
    ApiResponse handleCapture();
    ApiResponse handleSetISO(const std::string& isoValue);
    ApiResponse handleSetAperture(const std::string& apertureValue);
    ApiResponse handleSetShutterSpeed(const std::string& shutterValue);
    ApiResponse handleSetWhiteBalance(const std::string& wbValue);
    ApiResponse handleStartLiveView();
    ApiResponse handleStopLiveView();
    ApiResponse handleAutoFocus();
    ApiResponse handleZoomIn();
    ApiResponse handleZoomOut();
    ApiResponse handleGetCurrentISO();
    ApiResponse handleGetCurrentAperture();
    ApiResponse handleGetCurrentShutterSpeed();
    ApiResponse handleGetCurrentWhiteBalance();
    ApiResponse handleGetCurrentFocusMode();
    ApiResponse handleGetAllCurrentSettings();
};

// Forward declaration to avoid circular dependency
class CameraWebController;

// Live View and OSD Worker Thread Class
class LiveViewOSDWorker {
public:
    LiveViewOSDWorker(std::shared_ptr<CameraDevice> camera, const std::string& cameraId, CameraWebController* controller);
    ~LiveViewOSDWorker();

    void start();
    void stop();

    // Live view control
    void startLiveViewStreaming();
    void stopLiveViewStreaming();
    bool isLiveViewStreaming() const { return m_liveViewStreaming.load(); }

    // OSD control
    void enableOSD();
    void disableOSD();
    bool isOSDEnabled() const { return m_osdEnabled.load(); }

    // Frame access
    bool getLiveViewFrame(std::vector<uint8_t>& jpegData);
    bool getCompositeFrame(std::vector<uint8_t>& jpegData);
    void workerLoop(); // Made public for std::thread

private:
    std::shared_ptr<CameraDevice> m_camera;
    std::string m_cameraId;
    CameraWebController* m_controller;
    std::atomic<bool> m_running;

    // Live view streaming
    std::atomic<bool> m_liveViewStreaming;
    std::vector<uint8_t> m_latestFrame;
    std::mutex m_frameMutex;

    // OSD functionality
    std::atomic<bool> m_osdEnabled;
    std::vector<uint8_t> m_latestCompositeFrame;
    std::mutex m_compositeMutex;

    void captureAndProcessFrame();
    bool createOSDComposite(const std::vector<uint8_t>& liveViewData, std::vector<uint8_t>& compositeData);
};

} // namespace cli

#endif // CAMERAWEBCONTROLLER_H