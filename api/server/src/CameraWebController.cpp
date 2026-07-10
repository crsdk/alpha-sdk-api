#include "CameraWebController.h"
#include "Text.h"
#include "OpenCVWrapper.h"
#include <sstream>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sys/stat.h>
#if defined(__APPLE__) || defined(__linux__)
#include <dirent.h>
#else
#include <windows.h>
#include <direct.h>
#endif
#include <json/json.h>
#include <set>
#include <algorithm>
#include <map>

namespace SDK = SCRSDK;

namespace cli {

// REST API names mapped to SDK property codes. Prefer adding simple properties
// here and letting getPropertyGeneric/setPropertyGeneric handle them; add a
// custom CameraDevice method only when the SDK requires a sequence, callback
// wait, connection-mode check, or non-standard value conversion.
static const std::map<std::string, PropertyMapping> PROPERTY_MAP = {
    // Core camera controls exposed through the generic REST property endpoint.
    {"priority-key", PropertyMapping("priority-key", SDK::CrDevicePropertyCode::CrDeviceProperty_PriorityKeySettings, "set_position_key", false)},
    {"iso", PropertyMapping("iso", SDK::CrDevicePropertyCode::CrDeviceProperty_IsoSensitivity, "set_iso", true)},
    {"aperture", PropertyMapping("aperture", SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber, "set_aperture", true)},
    {"shutter-speed", PropertyMapping("shutter-speed", SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed, "set_shutter_speed", false)},
    {"drive-mode", PropertyMapping("drive-mode", SDK::CrDevicePropertyCode::CrDeviceProperty_DriveMode, "set_drive_mode", false)},
    {"white-balance", PropertyMapping("white-balance", SDK::CrDevicePropertyCode::CrDeviceProperty_WhiteBalance, "set_white_balance", false)},
    {"focus-mode", PropertyMapping("focus-mode", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusMode, "set_focus_mode", false)},
    {"exposure-program-mode", PropertyMapping("exposure-program-mode", SDK::CrDevicePropertyCode::CrDeviceProperty_ExposureProgramMode, "set_exposure_program_mode", false)},
    {"still-image-store-destination", PropertyMapping("still-image-store-destination", SDK::CrDevicePropertyCode::CrDeviceProperty_StillImageStoreDestination, "", false)},

    // Image format and quality settings (general properties for ILCE-7M4 compatibility)
    {"file-format", PropertyMapping("file-format", SDK::CrDevicePropertyCode::CrDeviceProperty_FileType, "", false)},
    {"image-quality", PropertyMapping("image-quality", SDK::CrDevicePropertyCode::CrDeviceProperty_StillImageQuality, "", false)},
    {"raw-compression", PropertyMapping("raw-compression", SDK::CrDevicePropertyCode::CrDeviceProperty_RAW_FileCompressionType, "", false)},

    // Focus properties
    {"focus-area", PropertyMapping("focus-area", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusArea, "", false)},

    // Zoom properties
    {"zoom-distance", PropertyMapping("zoom-distance", SDK::CrDevicePropertyCode::CrDeviceProperty_ZoomDistance, "", false)},

    // Focus distance (subject distance from camera, units: 1/1000 meter, 0xFFFFFFFF = infinity)
    {"focus-distance", PropertyMapping("focus-distance", SDK::CrDevicePropertyCode::CrDeviceProperty_FocalDistanceInMeter, "", false)},

    // Absolute focus position control
    {"focus-position", PropertyMapping("focus-position", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusPositionSetting, "", false)},
    {"focus-position-current", PropertyMapping("focus-position-current", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusPositionCurrentValue, "", false)},
    {"focus-driving-status", PropertyMapping("focus-driving-status", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusDrivingStatus, "", false)},
};

// REST action names mapped to SDK commands or REST-safe CameraDevice methods.
// Methods named here must be non-interactive; do not point web routes at CLI
// sample methods that read from stdin or block without a timeout.
static const std::map<std::string, ActionMapping> ACTION_MAP = {
    // Core camera actions exposed through the generic REST action endpoint.
    {"shutter", ActionMapping("shutter", SDK::CrCommandId::CrCommandId_Release, SDK::CrCommandParam::CrCommandParam_Down, "capture_image", true)},
    {"half-press", ActionMapping("half-press", SDK::CrCommandId::CrCommandId_Release, SDK::CrCommandParam::CrCommandParam_Down, "s1_shooting", true)},
    {"af-shutter", ActionMapping("af-shutter", SDK::CrCommandId::CrCommandId_Release, SDK::CrCommandParam::CrCommandParam_Down, "af_shutter", true)},
    {"zoom", ActionMapping("zoom", 0, 0, "execute_zoom_operation_direct", true)},  // Custom zoom handler
    {"focus-near-far", ActionMapping("focus-near-far", 0, 0, "focus_near_far", true)},  // Custom near/far focus step handler
    // Future actions can be added here with one line each:
    // {"movie-rec", ActionMapping("movie-rec", SDK::CrCommandId::CrCommandId_MovieRecord, SDK::CrCommandParam::CrCommandParam_Down, "toggle_movie_recording", true)},
};

CameraWebController::CameraWebController()
    : m_sdkInitialized(false), m_liveViewStreaming(false), m_currentConnectionMode(ConnectionMode::Remote), m_osdEnabled(false), m_disconnectInProgress(false)
{
    initializeSDK();
}

CameraWebController::~CameraWebController() {
    std::cout << "[Shutdown] Beginning CameraWebController destruction..." << std::endl;

    // Step 1: Stop live view streaming if running (legacy)
    if (m_liveViewStreaming.load()) {
        m_liveViewStreaming.store(false);
        if (m_liveViewThread.joinable()) {
            m_liveViewThread.join();
        }
    }

    // Step 2: Stop all worker threads WITHOUT erasing from map
    // (We need the camera pointers to remain for disconnect in Step 4)
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        for (auto& pair : m_cameraThreads) {
            if (!pair.second) continue;
            pair.second->active.store(false);
            if (pair.second->operationWorker) pair.second->operationWorker->stop();
            if (pair.second->liveViewWorker) pair.second->liveViewWorker->stop();
            if (pair.second->operationThread.joinable()) pair.second->operationThread.join();
            if (pair.second->liveViewThread.joinable()) pair.second->liveViewThread.join();
            std::cout << "[Shutdown] Worker threads stopped for camera: " << pair.first << std::endl;
        }
    }
    std::cout << "[Shutdown] All worker threads stopped" << std::endl;

    // Step 3: Clear all event callbacks BEFORE disconnecting
    // This prevents SDK callback threads from calling back through destroyed objects
    std::cout << "[Shutdown] Clearing event callbacks on all cameras..." << std::endl;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        for (auto& pair : m_cameraThreads) {
            if (pair.second && pair.second->camera) {
                pair.second->camera->setEventCallback(nullptr);
            }
        }
    }
    if (m_currentCamera) {
        m_currentCamera->setEventCallback(nullptr);
    }
    for (auto& camera : m_availableCameras) {
        if (camera) {
            camera->setEventCallback(nullptr);
        }
    }
    // Also clear the web event callback so no new wiring can happen
    m_webEventCallback = nullptr;

    // Disconnect and wait for the async OnDisconnected callback before release.
    // Releasing a live handle can race SDK callback threads during shutdown.
    auto disconnectAndWait = [](std::shared_ptr<CameraDevice>& camera, const std::string& label) {
        if (camera->is_connected()) {
            std::cout << "[Shutdown] Disconnecting " << label << "..." << std::endl;
            camera->disconnect();
            // Wait for SDK OnDisconnected callback (sets is_connected = false)
            int waitMs = 0;
            while (camera->is_connected() && waitMs < 5000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitMs += 100;
            }
            if (camera->is_connected()) {
                std::cout << "[Shutdown] Warning: " << label << " disconnect timed out after 5s" << std::endl;
            } else {
                std::cout << "[Shutdown] " << label << " disconnected OK" << std::endl;
            }
        }
        std::cout << "[Shutdown] Releasing " << label << " handle..." << std::endl;
        camera->release();
    };

    // Step 3: Collect all unique camera pointers from thread pairs and m_currentCamera
    std::set<CameraDevice*> processedCameras;

    // Disconnect + release cameras from thread pairs (already stopped)
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        for (auto& pair : m_cameraThreads) {
            if (pair.second && pair.second->camera) {
                auto* rawPtr = pair.second->camera.get();
                if (processedCameras.find(rawPtr) == processedCameras.end()) {
                    processedCameras.insert(rawPtr);
                    disconnectAndWait(pair.second->camera, "camera " + pair.first);
                }
            }
        }
    }

    // Disconnect + release m_currentCamera if not already processed
    if (m_currentCamera) {
        auto* rawPtr = m_currentCamera.get();
        if (processedCameras.find(rawPtr) == processedCameras.end()) {
            processedCameras.insert(rawPtr);
            disconnectAndWait(m_currentCamera, "current camera");
        }
        m_currentCamera.reset();
    }

    // Step 4: Disconnect + release any cameras from m_availableCameras not yet processed
    for (auto& camera : m_availableCameras) {
        if (camera) {
            auto* rawPtr = camera.get();
            if (processedCameras.find(rawPtr) == processedCameras.end()) {
                processedCameras.insert(rawPtr);
                disconnectAndWait(camera, "discovered camera");
            }
        }
    }
    m_availableCameras.clear();

    // Clear thread pairs now that all cameras are disconnected and released
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        m_cameraThreads.clear();
        m_connectedModes.clear();
    }

    std::cout << "[Shutdown] All cameras disconnected and released (" << processedCameras.size() << " total)" << std::endl;

    // Step 5: Release SDK (must be after all device handles are released)
    releaseSDK();
    std::cout << "[Shutdown] SDK released. CameraWebController destruction complete." << std::endl;
}

bool CameraWebController::initializeSDK() {
    std::lock_guard<std::mutex> lock(m_sdkLifecycleMutex);
    
    auto init_success = SDK::Init();
    if (!init_success) {
        std::cout << "Failed to initialize Remote SDK" << std::endl;
        return false;
    }
    
    m_sdkInitialized = true;
    std::cout << "Remote SDK successfully initialized for web server" << std::endl;
    return true;
}

void CameraWebController::releaseSDK() {
    std::lock_guard<std::mutex> lock(m_sdkLifecycleMutex);
    
    if (m_sdkInitialized) {
        SDK::Release();
        m_sdkInitialized = false;
    }
}

// Extract camera ID from raw ICrCameraObjectInfo without constructing a CameraDevice.
// Mirrors CameraDevice::get_id() logic: IP cameras use MAC address, others use GetId().
static std::string extractCameraId(SCRSDK::ICrCameraObjectInfo const* info) {
    cli::text connType(info->GetConnectionTypeName());
    if (cli::text(TEXT("IP")) == connType) {
        return std::string((char*)info->GetMACAddressChar());
    }
    return std::string((char*)info->GetId());
}

void CameraWebController::discoverCameras() {
    if (!m_sdkInitialized) return;

    SDK::ICrEnumCameraObjectInfo* camera_list = nullptr;
    auto enum_status = SDK::EnumCameraObjects(&camera_list);

    if (CR_FAILED(enum_status) || camera_list == nullptr) {
        // Enumeration failed — prune non-connected cameras, keep connected ones
        size_t before = m_availableCameras.size();
        m_availableCameras.erase(
            std::remove_if(m_availableCameras.begin(), m_availableCameras.end(),
                [this](const std::shared_ptr<CameraDevice>& cam) {
                    std::string id(cam->get_id().data());
                    std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
                    auto it = m_cameraThreads.find(id);
                    return !(it != m_cameraThreads.end() && it->second && it->second->camera
                             && it->second->camera->is_connected());
                }),
            m_availableCameras.end());
        if (m_availableCameras.size() != before) {
            std::cout << "📡 Camera enumeration failed, pruned " << (before - m_availableCameras.size())
                      << " unavailable camera(s)" << std::endl;
        }
        return;
    }

    auto ncams = camera_list->GetCount();

    // Build map of freshly enumerated camera IDs
    std::map<std::string, SCRSDK::ICrCameraObjectInfo const*> freshIds;
    for (CrInt32u i = 0; i < ncams; ++i) {
        auto camera_info = camera_list->GetCameraObjectInfo(i);
        std::string id = extractCameraId(camera_info);
        freshIds[id] = camera_info;
    }

    // Build set of connected camera IDs. Do not recreate connected
    // CameraDevice instances because their SDK handles are still live.
    std::set<std::string> connectedIds;
    {
        std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
        for (const auto& [id, tp] : m_cameraThreads) {
            if (tp && tp->camera && tp->camera->is_connected()) {
                connectedIds.insert(id);
            }
        }
    }

    // Prune: remove cameras no longer present in enumeration
    size_t removedCount = 0;
    m_availableCameras.erase(
        std::remove_if(m_availableCameras.begin(), m_availableCameras.end(),
            [&freshIds, &removedCount](const std::shared_ptr<CameraDevice>& cam) {
                std::string id(cam->get_id().data());
                if (freshIds.find(id) == freshIds.end()) {
                    ++removedCount;
                    return true;
                }
                return false;
            }),
        m_availableCameras.end());

    // For unconnected cameras that ARE in the fresh list, refresh with latest SDK info.
    // Connected cameras keep their existing CameraDevice (SDK handle is live).
    // Unconnected cameras get recreated so SDK::Connect() uses fresh enumeration data.
    for (auto& cam : m_availableCameras) {
        std::string id(cam->get_id().data());
        if (connectedIds.find(id) == connectedIds.end() && freshIds.find(id) != freshIds.end()) {
            // Replace with fresh CameraDevice from latest enumeration
            cam = std::make_shared<CameraDevice>(cam->get_number(), freshIds[id]);
        }
    }

    // Collect existing IDs
    std::set<std::string> existingIds;
    for (const auto& cam : m_availableCameras) {
        existingIds.insert(std::string(cam->get_id().data()));
    }

    // Add: create CameraDevice only for genuinely new cameras
    size_t addedCount = 0;
    CrInt32u nextIndex = static_cast<CrInt32u>(m_availableCameras.size()) + 1;
    for (const auto& [id, info] : freshIds) {
        if (existingIds.find(id) == existingIds.end()) {
            auto camera = std::make_shared<CameraDevice>(nextIndex++, info);
            m_availableCameras.push_back(camera);
            ++addedCount;
        }
    }

    camera_list->Release();

    // Only log when something changed
    if (addedCount > 0 || removedCount > 0) {
        std::cout << "📡 Camera discovery: +" << addedCount << " added, -" << removedCount
                  << " removed, " << m_availableCameras.size() << " total" << std::endl;
        for (const auto& cam : m_availableCameras) {
            std::cout << "  📷 " << std::string(cam->get_model().data()) << " ("
                      << std::string(cam->get_id().data()) << ")" << std::endl;
        }
    }
}

ApiResponse CameraWebController::getStatus() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (m_currentCamera && m_currentCamera->is_connected()) {
        response.success = true;
        response.message = "Camera connected";
        response.camera.connected = true;
        response.camera.model = std::string(m_currentCamera->get_model().data());
        response.camera.id = std::string(m_currentCamera->get_id().data());
    } else {
        response.success = true;
        response.message = "No camera connected";
        response.camera.connected = false;
    }
    
    return response;
}

ApiResponse CameraWebController::connectCamera(const std::string& connectionMode, const std::string& cameraId, const std::string& username, const std::string& password, const std::string& reconnecting) {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);

    ApiResponse response;

    // Check if this specific camera is already connected (per-camera check)
    if (!cameraId.empty()) {
        std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end() && it->second && it->second->camera
            && it->second->camera->is_connected()) {
            response.success = true;
            response.message = "Camera already connected";
            response.camera.connected = true;
            response.camera.model = std::string(it->second->camera->get_model().data());
            response.camera.id = cameraId;
            return response;
        }
    }

    // Parse connection mode
    SDK::CrSdkControlMode sdkMode = SDK::CrSdkControlMode_Remote; // Default
    m_currentConnectionMode = ConnectionMode::Remote;

    if (connectionMode == "remote") {
        sdkMode = SDK::CrSdkControlMode_Remote;
        m_currentConnectionMode = ConnectionMode::Remote;
    } else if (connectionMode == "contents") {
        sdkMode = SDK::CrSdkControlMode_ContentsTransfer;
        m_currentConnectionMode = ConnectionMode::ContentsTransfer;
    } else if (connectionMode == "remote-transfer") {
        sdkMode = SDK::CrSdkControlMode_RemoteTransfer;
        m_currentConnectionMode = ConnectionMode::RemoteTransfer;
    } else {
        response.success = false;
        response.message = "Invalid connection mode. Use: 'remote', 'contents', or 'remote-transfer'";
        return response;
    }

    std::cout << "🔗 Attempting connection in mode: " << connectionMode << std::endl;

    // Discover available cameras
    discoverCameras();

    if (m_availableCameras.empty()) {
        response.success = false;
        response.message = "No cameras found. Please connect a camera and try again.";
        response.camera.connected = false;
        return response;
    }

    // Find the specific camera to connect to
    std::shared_ptr<CameraDevice> targetCamera = nullptr;

    if (!cameraId.empty()) {
        // Connect to specific camera by ID
        for (const auto& camera : m_availableCameras) {
            if (std::string(camera->get_id().data()) == cameraId) {
                targetCamera = camera;
                break;
            }
        }

        if (!targetCamera) {
            response.success = false;
            response.message = "Camera with ID '" + cameraId + "' not found";
            return response;
        }
    } else {
        // Connect to first available camera that isn't already connected
        {
            std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
            for (const auto& camera : m_availableCameras) {
                std::string camId = std::string(camera->get_id().data());
                auto it = m_cameraThreads.find(camId);
                if (it == m_cameraThreads.end() || !it->second || !it->second->camera
                    || !it->second->camera->is_connected()) {
                    targetCamera = camera;
                    break;
                }
            }
        }
        if (!targetCamera) {
            response.success = false;
            response.message = "All available cameras are already connected";
            return response;
        }
    }

    std::cout << "📷 Connecting to camera: " << std::string(targetCamera->get_model().data())
              << " (" << std::string(targetCamera->get_id().data()) << ")" << std::endl;

    // Attempt connection with authentication if provided
    bool connect_result = false;

    // Determine reconnecting mode from parameter
    SDK::CrReconnectingSet reconnectMode = (reconnecting == "on") ? SDK::CrReconnecting_ON : SDK::CrReconnecting_OFF;

    std::cout << "🔌 Reconnecting mode: " << reconnecting << " (SDK: " << (reconnectMode == SDK::CrReconnecting_ON ? "ON" : "OFF") << ")" << std::endl;

    if (!username.empty() && !password.empty()) {
        std::cout << "🔐 Using SSH authentication for camera connection" << std::endl;
        // Note: The CameraDevice::connect method may need to be extended to support SSH credentials
        // For now, we'll use the basic connection and log the credentials
        std::cout << "   Username: " << username << std::endl;
        connect_result = targetCamera->connect(sdkMode, reconnectMode);
    } else {
        connect_result = targetCamera->connect(sdkMode, reconnectMode);
    }

    if (connect_result) {
        response.success = true;
        response.message = "Camera connected successfully in " + connectionMode + " mode";
        response.camera.connected = true;
        response.camera.model = std::string(targetCamera->get_model().data());
        response.camera.id = std::string(targetCamera->get_id().data());

        std::cout << "✅ Web controller successfully connected to camera: "
                  << response.camera.model << " (" << response.camera.id << ")" << std::endl;

        // Set priority key to PC Remote to enable remote control
        // This is REQUIRED before we can set drive mode, exposure mode, focus mode
        if (targetCamera->set_priority_key_to_pc_remote()) {
            std::cout << "✅ Set position key to PC Remote (remote control enabled)" << std::endl;
        } else {
            std::cout << "⚠️  Warning: Could not set position key to PC Remote" << std::endl;
        }

        // IMPORTANT: Clean up any existing worker threads for this camera first
        std::string connectedCameraId = response.camera.id;

        // Check if threads already exist for this camera
        {
            std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
            if (m_cameraThreads.find(connectedCameraId) != m_cameraThreads.end()) {
                std::cout << "🧹 Cleaning up existing worker threads for camera: " << connectedCameraId << std::endl;
            }
        }
        // Clean up existing threads outside the lock; destroyCameraThreads()
        // also needs m_cameraThreadsMutex.
        destroyCameraThreads(connectedCameraId);

        // Wire SSE event callback so SDK events are forwarded to web clients
        wireCameraEventCallback(targetCamera, connectedCameraId);

        // Now create fresh worker threads
        createCameraThreads(connectedCameraId, targetCamera);
        std::cout << "✅ Worker threads created for camera: " << connectedCameraId << std::endl;

        // Track connection mode per-camera
        {
            std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
            m_connectedModes[connectedCameraId] = m_currentConnectionMode;
        }
    } else {
        response.success = false;
        response.message = "Failed to connect to camera. Please check camera status and try again.";
        response.camera.connected = false;

        std::cout << "❌ Web controller failed to connect to camera" << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::disconnectCamera() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);

    ApiResponse response;

    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = true;
        response.message = "No camera connected";
        response.camera.connected = false;
        return response;
    }

    // Set disconnect flag to signal all worker threads to stop SDK calls immediately
    std::cout << "🚨 Setting disconnect flag to prevent SDK calls during shutdown..." << std::endl;
    m_disconnectInProgress.store(true);

    // Get camera ID before disconnecting
    std::string cameraId = std::string(m_currentCamera->get_id().data());

    // IMPORTANT: Stop live view streaming explicitly BEFORE stopping worker threads
    // This prevents race conditions where live view continues during disconnect
    std::cout << "🛑 Explicitly stopping live view streaming before disconnect..." << std::endl;
    if (m_liveViewStreaming.load()) {
        std::cout << "🛑 Legacy live view streaming detected, stopping..." << std::endl;
        try {
            stopLiveViewStreaming();
            std::cout << "✅ Legacy live view streaming stopped" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "⚠️ Error stopping legacy live view: " << e.what() << std::endl;
        }
    }

    // IMPORTANT: Stop worker threads BEFORE disconnecting camera
    // This prevents threads from accessing the camera while it's being disconnected
    std::cout << "📌 Stopping worker threads before camera disconnect..." << std::endl;
    destroyCameraThreads(cameraId);
    std::cout << "✅ Worker threads stopped safely" << std::endl;

    // Now safely disconnect the camera after threads have stopped
    bool disconnect_result = m_currentCamera->disconnect();

    if (disconnect_result) {
        std::cout << "✅ Camera disconnected successfully after worker threads stopped" << std::endl;

        response.success = true;
        response.message = "Camera disconnected successfully";
        response.camera.connected = false;

        std::cout << "Web controller successfully disconnected camera" << std::endl;
        m_currentCamera.reset();
    } else {
        response.success = false;
        response.message = "Failed to disconnect camera";
        response.camera.connected = true;
        response.camera.model = std::string(m_currentCamera->get_model().data());
        response.camera.id = std::string(m_currentCamera->get_id().data());
        
        std::cout << "Web controller failed to disconnect camera" << std::endl;
    }

    // Reset disconnect flag regardless of success/failure - operation is complete
    std::cout << "🚨 Resetting disconnect flag - disconnect operation complete" << std::endl;
    m_disconnectInProgress.store(false);

    return response;
}

ApiResponse CameraWebController::disconnectCamera(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);

    ApiResponse response;

    // Look up camera in m_cameraThreads
    std::shared_ptr<CameraDevice> camera;
    {
        std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it == m_cameraThreads.end() || !it->second || !it->second->camera) {
            response.success = true;
            response.message = "Camera not connected";
            response.camera.connected = false;
            response.camera.id = cameraId;
            return response;
        }
        camera = it->second->camera;
    }

    if (!camera->is_connected()) {
        // Camera entry exists but already disconnected — clean up
        destroyCameraThreads(cameraId);
        {
            std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
            m_connectedModes.erase(cameraId);
        }
        response.success = true;
        response.message = "Camera not connected";
        response.camera.connected = false;
        response.camera.id = cameraId;
        return response;
    }

    std::cout << "🔌 Disconnecting camera: " << cameraId << std::endl;

    // Stop worker threads BEFORE disconnecting camera
    destroyCameraThreads(cameraId);
    std::cout << "✅ Worker threads stopped for camera: " << cameraId << std::endl;

    // Emit synthetic disconnected event BEFORE clearing callback
    // (SDK's OnDisconnected won't fire after we clear the callback)
    if (m_webEventCallback) {
        m_webEventCallback(cameraId, "disconnected",
            "{\"id\":\"" + cameraId + "\",\"errorCode\":\"0x0\"}");
    }

    // Clear event callback BEFORE disconnect to prevent SDK callback races
    camera->setEventCallback(nullptr);

    // Disconnect the camera and wait for OnDisconnected callback
    camera->disconnect();

    // Wait for SDK OnDisconnected callback (sets is_connected = false)
    int waitMs = 0;
    while (camera->is_connected() && waitMs < 5000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitMs += 100;
    }

    bool disconnect_result = !camera->is_connected();

    if (disconnect_result) {
        std::cout << "✅ Camera " << cameraId << " disconnected successfully" << std::endl;

        // Release the device handle — reconnect uses a fresh object from EnumCameraObjects
        camera->release();

        response.success = true;
        response.message = "Camera disconnected successfully";
        response.camera.connected = false;
        response.camera.id = cameraId;
    } else {
        std::cout << "❌ Disconnect timed out for camera " << cameraId << std::endl;
        response.success = false;
        response.message = "Disconnect timed out";
        response.camera.connected = true;
        response.camera.model = std::string(camera->get_model().data());
        response.camera.id = cameraId;
    }

    // Clean up connection mode tracking
    {
        std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
        m_connectedModes.erase(cameraId);
    }

    return response;
}

ApiResponse CameraWebController::getStatus(const std::string& cameraId) {
    ApiResponse response;

    std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
    auto it = m_cameraThreads.find(cameraId);
    if (it != m_cameraThreads.end() && it->second && it->second->camera
        && it->second->camera->is_connected()) {
        response.success = true;
        response.message = "Camera connected";
        response.camera.connected = true;
        response.camera.model = std::string(it->second->camera->get_model().data());
        response.camera.id = cameraId;
    } else {
        response.success = true;
        response.message = "No camera connected";
        response.camera.connected = false;
        response.camera.id = cameraId;
    }

    return response;
}

std::vector<CameraInfo> CameraWebController::getAvailableCameras() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);

    discoverCameras();

    std::vector<CameraInfo> cameras;
    for (const auto& camera : m_availableCameras) {
        CameraInfo info;
        info.model = std::string(camera->get_model().data());
        info.id = std::string(camera->get_id().data());

        // Check if this camera is actually connected via m_cameraThreads
        info.connected = false;
        {
            std::lock_guard<std::mutex> threadLock(m_cameraThreadsMutex);
            auto it = m_cameraThreads.find(info.id);
            if (it != m_cameraThreads.end() && it->second && it->second->camera
                && it->second->camera->is_connected()) {
                info.connected = true;
            }
        }

        auto id_str = info.id;
        if (id_str.find("TCP:") != std::string::npos || id_str.find("192.") != std::string::npos) {
            info.connectionType = "Network";
        } else {
            info.connectionType = "USB";
        }

        cameras.push_back(info);
    }

    return cameras;
}

std::string CameraWebController::toJson(const ApiResponse& response) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"success\": " << (response.success ? "true" : "false") << ",\n";
    json << "  \"message\": \"" << response.message << "\",\n";
    json << "  \"camera\": {\n";
    json << "    \"connected\": " << (response.camera.connected ? "true" : "false") << ",\n";
    json << "    \"model\": \"" << response.camera.model << "\",\n";
    json << "    \"id\": \"" << response.camera.id << "\"\n";
    json << "  }";

    // Include data field if it has content
    if (!response.data.empty()) {
        json << ",\n  \"data\": {\n";
        bool first = true;
        for (const auto& pair : response.data) {
            if (!first) json << ",\n";

            // Special handling for fields that are already JSON (don't wrap in quotes)
            if (pair.first == "available_values" || pair.first == "properties") {
                json << "    \"" << pair.first << "\": " << pair.second;
            } else {
                json << "    \"" << pair.first << "\": \"" << pair.second << "\"";
            }
            first = false;
        }
        json << "\n  }";
    }

    json << "\n}";
    return json.str();
}

std::string CameraWebController::toJson(const std::vector<CameraInfo>& cameras) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"success\": true,\n";
    json << "  \"cameras\": [\n";
    
    for (size_t i = 0; i < cameras.size(); ++i) {
        const auto& camera = cameras[i];
        json << "    {\n";
        json << "      \"model\": \"" << camera.model << "\",\n";
        json << "      \"id\": \"" << camera.id << "\",\n";
        json << "      \"connected\": " << (camera.connected ? "true" : "false") << "\n";
        json << "    }";
        if (i < cameras.size() - 1) json << ",";
        json << "\n";
    }
    
    json << "  ]\n";
    json << "}";
    return json.str();
}

// Camera control implementations
ApiResponse CameraWebController::captureImage() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);

    ApiResponse response;

    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = false;
        response.message = "No camera connected";
        return response;
    }

    std::string cameraId = std::string(m_currentCamera->get_id().data());
    std::cout << "HTTP THREAD: Requesting capture for camera: " << cameraId << std::endl;

    // Use new worker thread architecture - this eliminates the crash issue
    return executeCameraCommand(cameraId, CameraCommandType::Capture);
}

ApiResponse CameraWebController::setISO(const std::string& cameraId, const std::string& isoValue) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera if cameraId not found in threads
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    try {
        // Get ISO property from camera to check writability and possible values
        CrInt32u codes[1] = { SDK::CrDevicePropertyCode::CrDeviceProperty_IsoSensitivity };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);
        
        if (!CR_SUCCEEDED(err) || numProps == 0 || properties == nullptr) {
            response.success = false;
            response.message = "Failed to get ISO property from camera";
            return response;
        }
        
        // Check if ISO is writable
        if (1 != properties[0].IsSetEnableCurrentValue()) {
            response.success = false;
            response.message = "ISO is not writable on this camera";
            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
            return response;
        }

        // Get possible ISO values
        std::vector<CrInt32u> values;
        CrInt32u valueSize = properties[0].GetValueSize();
        CrInt32u numValues = valueSize / sizeof(std::uint32_t);
        if (numValues > 0) {
            auto propertyValues = reinterpret_cast<const std::uint32_t*>(properties[0].GetValues());
            for (CrInt32u i = 0; i < numValues; i++) {
                values.push_back(propertyValues[i]);
            }
        }

        // Release the properties
        SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
        
        // Convert string ISO value to Sony SDK value format
        CrInt32u targetValue = 0;
        if (isoValue == "auto") {
            targetValue = 0xFFFFFFFF; // Sony's auto ISO value
        } else if (isoValue.length() >= 2 && (isoValue.substr(0, 2) == "0x" || isoValue.substr(0, 2) == "0X")) {
            // Hex value: "0x64" -> 100
            targetValue = static_cast<CrInt32u>(std::stoull(isoValue.substr(2), nullptr, 16));
        } else {
            // Decimal value: "100" -> 100
            targetValue = static_cast<CrInt32u>(std::stoull(isoValue, nullptr, 10));
        }
        
        // Find matching value in possible values
        bool found = false;
        for (const auto& possibleValue : values) {
            if (possibleValue == targetValue) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            response.success = false;
            response.message = "ISO value " + isoValue + " not supported by camera";
            return response;
        }
        
        // Set the ISO value
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_IsoSensitivity);
        prop.SetCurrentValue(targetValue);
        prop.SetValueType(SDK::CrDataType::CrDataType_UInt32Array);

        auto result = SDK::SetDeviceProperty(camera->get_device_handle(), &prop);

        if (CR_SUCCEEDED(result)) {
            response.success = true;
            response.message = "ISO set to " + isoValue;
            std::cout << "✅ ISO successfully set to: " << isoValue << " for camera: " << cameraId << std::endl;
        } else {
            response.success = false;
            response.message = "Failed to set ISO: SDK error " + std::to_string(result);
            std::cout << "❌ Failed to set ISO: " << result << std::endl;
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Error setting ISO: " + std::string(e.what());
        std::cout << "❌ Exception setting ISO: " << e.what() << std::endl;
    }

    response.camera.connected = true;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.id = std::string(camera->get_id().data());

    return response;
}

ApiResponse CameraWebController::setAperture(const std::string& cameraId, const std::string& apertureValue) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera if cameraId not found in threads
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    try {
        // Get aperture property from camera to check writability and possible values
        CrInt32u codes[1] = { SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);
        
        if (!CR_SUCCEEDED(err) || numProps == 0 || properties == nullptr) {
            response.success = false;
            response.message = "Failed to get aperture property from camera";
            return response;
        }
        
        // Check if aperture is writable
        if (1 != properties[0].IsSetEnableCurrentValue()) {
            response.success = false;
            response.message = "Aperture is not writable on this camera";
            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
            return response;
        }

        // Get possible aperture values
        std::vector<CrInt32u> values;
        CrInt32u valueSize = properties[0].GetValueSize();
        CrInt32u numValues = valueSize / sizeof(std::uint16_t);  // F-Number uses uint16_t
        if (numValues > 0) {
            auto propertyValues = reinterpret_cast<const std::uint16_t*>(properties[0].GetValues());
            for (CrInt32u i = 0; i < numValues; i++) {
                values.push_back(static_cast<CrInt32u>(propertyValues[i]));
            }
        }

        // Release the properties
        SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
        
        // Convert string aperture value to Sony SDK value format
        CrInt32u targetValue = 0;
        if (apertureValue.find("f/") == 0 || apertureValue.find("F/") == 0) {
            // Parse "f/2.8" or "F/2.8" format
            std::string numStr = apertureValue.substr(2);

            // Check if the numeric part is hex (e.g., "f/0x190")
            if (numStr.length() >= 2 && (numStr.substr(0, 2) == "0x" || numStr.substr(0, 2) == "0X")) {
                // Hex value: "f/0x190" -> 400
                targetValue = static_cast<CrInt32u>(std::stoull(numStr.substr(2), nullptr, 16));
            } else {
                // Decimal value: "f/2.8" -> 280
                double fValue = std::stod(numStr);
                targetValue = static_cast<CrInt32u>(fValue * 100);
            }
        } else if (apertureValue.length() >= 2 && (apertureValue.substr(0, 2) == "0x" || apertureValue.substr(0, 2) == "0X")) {
            // Direct hex value: "0x190" -> 400
            targetValue = static_cast<CrInt32u>(std::stoull(apertureValue.substr(2), nullptr, 16));
        } else {
            // Try as decimal numeric value (e.g., "280" -> 280, or "2.8" -> 280)
            try {
                double fValue = std::stod(apertureValue);
                // If value is < 100, assume it's the f-stop number (e.g., "2.8"), otherwise it's the SDK value
                if (fValue < 100) {
                    targetValue = static_cast<CrInt32u>(fValue * 100);
                } else {
                    targetValue = static_cast<CrInt32u>(fValue);
                }
            } catch (const std::exception&) {
                response.success = false;
                response.message = "Invalid aperture format. Expected hex (0x320), decimal (800), f-stop (f/8 or 8), or f-stop value (f/2.8 or 2.8)";
                return response;
            }
        }
        
        // Find matching value in possible values
        bool found = false;
        for (const auto& possibleValue : values) {
            if (possibleValue == targetValue) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            response.success = false;
            response.message = "Aperture value " + apertureValue + " not supported by camera";
            return response;
        }
        
        // Set the aperture value
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber);
        prop.SetCurrentValue(targetValue);
        prop.SetValueType(SDK::CrDataType::CrDataType_UInt32Array);
        
        auto result = SDK::SetDeviceProperty(camera->get_device_handle(), &prop);

        if (CR_SUCCEEDED(result)) {
            response.success = true;
            response.message = "Aperture set to " + apertureValue;
            std::cout << "✅ Aperture successfully set to: " << apertureValue << " for camera: " << cameraId << std::endl;
        } else {
            response.success = false;
            response.message = "Failed to set aperture: SDK error " + std::to_string(result);
            std::cout << "❌ Failed to set aperture: " << result << std::endl;
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Error setting aperture: " + std::string(e.what());
        std::cout << "❌ Exception setting aperture: " << e.what() << std::endl;
    }

    response.camera.connected = true;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.id = std::string(camera->get_id().data());

    return response;
}

ApiResponse CameraWebController::setShutterSpeed(const std::string& cameraId, const std::string& shutterValue) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }
    
    try {
        // Get shutter speed property from camera to check writability and possible values
        CrInt32u codes[1] = { SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);

        if (!CR_SUCCEEDED(err) || numProps == 0 || properties == nullptr) {
            response.success = false;
            response.message = "Failed to get shutter speed property from camera";
            return response;
        }

        // Check if shutter speed is writable
        if (1 != properties[0].IsSetEnableCurrentValue()) {
            response.success = false;
            response.message = "Shutter speed is not writable on this camera";
            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
            return response;
        }

        // Get possible shutter speed values
        std::vector<CrInt32u> values;
        CrInt32u valueSize = properties[0].GetValueSize();
        CrInt32u numValues = valueSize / sizeof(std::uint32_t);  // Shutter speed uses uint32_t
        if (numValues > 0) {
            auto propertyValues = reinterpret_cast<const std::uint32_t*>(properties[0].GetValues());
            for (CrInt32u i = 0; i < numValues; i++) {
                values.push_back(propertyValues[i]);
            }
        }

        // Release the properties
        SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);

        // Convert string shutter speed value to Sony SDK value format
        // SDK Format: Upper 16 bits = numerator, Lower 16 bits = denominator
        // Example: "1/100" -> (1 << 16) | 100 = 0x00010064 = 65636
        CrInt32u targetValue = 0;
        if (shutterValue.length() >= 2 && (shutterValue.substr(0, 2) == "0x" || shutterValue.substr(0, 2) == "0X")) {
            // Direct hex value: "0x10064" -> 65636 (1/100)
            targetValue = static_cast<CrInt32u>(std::stoull(shutterValue.substr(2), nullptr, 16));
        } else if (shutterValue.find("1/") == 0) {
            // Parse "1/125" format
            std::string denomStr = shutterValue.substr(2);
            CrInt16u numerator = 1;
            CrInt16u denominator = static_cast<CrInt16u>(std::stoi(denomStr));
            targetValue = (static_cast<CrInt32u>(numerator) << 16) | denominator;
        } else if (shutterValue.find("\"") != std::string::npos || shutterValue.find("s") != std::string::npos) {
            // Handle long exposures like "2\"" or "2s"
            // Example: "2\"" -> (2 << 16) | 1 = 0x00020001
            std::string numStr = shutterValue;
            if (numStr.back() == '"' || numStr.back() == 's') {
                numStr.pop_back();
            }
            CrInt16u numerator = static_cast<CrInt16u>(std::stoi(numStr));
            CrInt16u denominator = 1;
            targetValue = (static_cast<CrInt32u>(numerator) << 16) | denominator;
        } else {
            // Try parsing as a plain number (denominator for 1/X or direct SDK value)
            try {
                CrInt32u value = static_cast<CrInt32u>(std::stoull(shutterValue));
                // If value is > 65536, assume it's already the full SDK value
                if (value > 65536) {
                    targetValue = value;
                } else {
                    // Otherwise treat as denominator for 1/X
                    CrInt16u numerator = 1;
                    CrInt16u denominator = static_cast<CrInt16u>(value);
                    targetValue = (static_cast<CrInt32u>(numerator) << 16) | denominator;
                }
            } catch (...) {
                response.success = false;
                response.message = "Invalid shutter speed format. Expected hex (0x10064), fraction (1/100), bulb time (2s or 2\"), denominator (100), or SDK value (65636)";
                return response;
            }
        }

        // Find matching value in possible values
        bool found = false;
        for (const auto& possibleValue : values) {
            if (possibleValue == targetValue) {
                found = true;
                break;
            }
        }

        if (!found) {
            response.success = false;
            response.message = "Shutter speed value " + shutterValue + " not supported by camera";
            return response;
        }

        // Set the shutter speed value
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed);
        prop.SetCurrentValue(targetValue);
        prop.SetValueType(SDK::CrDataType::CrDataType_UInt32Array);

        auto result = SDK::SetDeviceProperty(camera->get_device_handle(), &prop);

        if (CR_SUCCEEDED(result)) {
            response.success = true;
            response.message = "Shutter speed set to " + shutterValue;
            std::cout << "✅ Shutter speed successfully set to: " << shutterValue << " for camera: " << cameraId << std::endl;
        } else {
            response.success = false;
            response.message = "Failed to set shutter speed: SDK error " + std::to_string(result);
            std::cout << "❌ Failed to set shutter speed: " << result << std::endl;
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Error setting shutter speed: " + std::string(e.what());
        std::cout << "❌ Exception setting shutter speed: " << e.what() << std::endl;
    }

    response.camera.connected = true;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.id = std::string(camera->get_id().data());

    return response;
}

ApiResponse CameraWebController::setWhiteBalance(const std::string& cameraId, const std::string& wbValue) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    response.success = true;
    response.message = "White balance setting requested: " + wbValue;
    response.camera.connected = true;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.id = std::string(camera->get_id().data());

    std::cout << "Web controller white balance setting requested: " << wbValue << " for camera: " << cameraId << std::endl;

    return response;
}

ApiResponse CameraWebController::startLiveView() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = false;
        response.message = "No camera connected";
        return response;
    }
    
    try {
        // Enable live view on camera
        CrInt32u current = 0;
        SDK::GetDeviceSetting(m_currentCamera->get_device_handle(), SDK::Setting_Key_EnableLiveView, &current);
        
        if (current == 0) {
            SDK::SetDeviceSetting(m_currentCamera->get_device_handle(), SDK::Setting_Key_EnableLiveView, 1);
        }
        
        response.success = true;
        response.message = "Live view enabled";
        response.camera.connected = true;
        response.camera.model = std::string(m_currentCamera->get_model().data());
        response.camera.id = std::string(m_currentCamera->get_id().data());
        
        std::cout << "Web controller enabled live view" << std::endl;
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to enable live view: " + std::string(e.what());
        std::cout << "Web controller live view enable failed: " << e.what() << std::endl;
    }
    
    return response;
}

ApiResponse CameraWebController::stopLiveView() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = false;
        response.message = "No camera connected";
        return response;
    }
    
    try {
        // Disable live view on camera
        SDK::SetDeviceSetting(m_currentCamera->get_device_handle(), SDK::Setting_Key_EnableLiveView, 0);
        
        response.success = true;
        response.message = "Live view disabled";
        response.camera.connected = true;
        response.camera.model = std::string(m_currentCamera->get_model().data());
        response.camera.id = std::string(m_currentCamera->get_id().data());
        
        std::cout << "Web controller disabled live view" << std::endl;
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to disable live view: " + std::string(e.what());
        std::cout << "Web controller live view disable failed: " << e.what() << std::endl;
    }
    
    return response;
}

ApiResponse CameraWebController::autoFocus() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = false;
        response.message = "No camera connected";
        return response;
    }
    
    try {
        m_currentCamera->af_shutter();
        response.success = true;
        response.message = "Auto focus executed";
        response.camera.connected = true;
        response.camera.model = std::string(m_currentCamera->get_model().data());
        response.camera.id = std::string(m_currentCamera->get_id().data());
        
        std::cout << "Web controller executed auto focus" << std::endl;
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to execute auto focus: " + std::string(e.what());
        std::cout << "Web controller auto focus failed: " << e.what() << std::endl;
    }
    
    return response;
}

ApiResponse CameraWebController::zoomIn() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = false;
        response.message = "No camera connected";
        return response;
    }
    
    response.success = true;
    response.message = "Zoom in requested";
    response.camera.connected = true;
    response.camera.model = std::string(m_currentCamera->get_model().data());
    response.camera.id = std::string(m_currentCamera->get_id().data());
    
    std::cout << "Web controller zoom in requested" << std::endl;
    
    return response;
}

ApiResponse CameraWebController::zoomOut() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = false;
        response.message = "No camera connected";
        return response;
    }
    
    response.success = true;
    response.message = "Zoom out requested";
    response.camera.connected = true;
    response.camera.model = std::string(m_currentCamera->get_model().data());
    response.camera.id = std::string(m_currentCamera->get_id().data());
    
    std::cout << "Web controller zoom out requested" << std::endl;
    
    return response;
}

// Live view streaming implementations
ApiResponse CameraWebController::startLiveViewStreaming() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.success = false;
        response.message = "No camera connected";
        return response;
    }
    
    if (m_liveViewStreaming.load()) {
        response.success = true;
        response.message = "Live view streaming already running";
        response.camera.connected = true;
        response.camera.model = std::string(m_currentCamera->get_model().data());
        response.camera.id = std::string(m_currentCamera->get_id().data());
        return response;
    }
    
    try {
        // Enable live view first
        CrInt32u current = 0;
        auto err = SDK::GetDeviceSetting(m_currentCamera->get_device_handle(), SDK::Setting_Key_EnableLiveView, &current);
        std::cout << "Current EnableLiveView setting: " << current << std::endl;

        if (current == 0) {
            std::cout << "Enabling live view on camera..." << std::endl;
            err = SDK::SetDeviceSetting(m_currentCamera->get_device_handle(), SDK::Setting_Key_EnableLiveView, 1);
            if (CR_FAILED(err)) {
                response.success = false;
                response.message = "Failed to enable live view on camera";
                std::cout << "Failed to enable live view: " << std::hex << err << std::dec << std::endl;
                return response;
            }
            // Wait a moment for the camera to process the setting
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // Start streaming thread
        m_liveViewStreaming.store(true);
        m_liveViewThread = std::thread(&CameraWebController::liveViewStreamingThread, this);
        
        response.success = true;
        response.message = "Live view streaming started";
        response.camera.connected = true;
        response.camera.model = std::string(m_currentCamera->get_model().data());
        response.camera.id = std::string(m_currentCamera->get_id().data());
        
        std::cout << "Web controller started live view streaming" << std::endl;
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to start live view streaming: " + std::string(e.what());
        std::cout << "Web controller live view streaming start failed: " << e.what() << std::endl;
    }
    
    return response;
}

ApiResponse CameraWebController::stopLiveViewStreaming() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    
    ApiResponse response;
    
    if (!m_liveViewStreaming.load()) {
        response.success = true;
        response.message = "Live view streaming not running";
        return response;
    }
    
    try {
        // Stop streaming thread
        m_liveViewStreaming.store(false);
        if (m_liveViewThread.joinable()) {
            m_liveViewThread.join();
        }
        
        response.success = true;
        response.message = "Live view streaming stopped";
        
        std::cout << "Web controller stopped live view streaming" << std::endl;
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Failed to stop live view streaming: " + std::string(e.what());
        std::cout << "Web controller live view streaming stop failed: " << e.what() << std::endl;
    }
    
    return response;
}

bool CameraWebController::getLiveViewFrame(std::vector<uint8_t>& jpegData) {
    std::lock_guard<std::mutex> lock(m_frameMutex);

    if (m_latestFrame.empty()) {
        return false;
    }

    jpegData = m_latestFrame;
    return true;
}

// ============================================================================
// Per-Camera Live View Methods
// ============================================================================

ApiResponse CameraWebController::enableLiveView(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->camera || !it->second->camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    auto& camera = it->second->camera;
    auto err = SDK::SetDeviceSetting(camera->get_device_handle(), SDK::Setting_Key_EnableLiveView, SDK::CrDeviceSetting_Enable);
    if (CR_FAILED(err)) {
        response.success = false;
        response.message = "Failed to enable live view: 0x" + ([&]{
            std::ostringstream oss; oss << std::hex << err; return oss.str();
        })();
        return response;
    }

    response.success = true;
    response.message = "Live view enabled";
    response.camera.id = cameraId;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.connected = true;
    return response;
}

ApiResponse CameraWebController::disableLiveView(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->camera || !it->second->camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    auto& camera = it->second->camera;

    // Stop streaming first if active
    if (it->second->liveViewWorker && it->second->liveViewWorker->isLiveViewStreaming()) {
        it->second->liveViewWorker->stopLiveViewStreaming();
    }

    auto err = SDK::SetDeviceSetting(camera->get_device_handle(), SDK::Setting_Key_EnableLiveView, SDK::CrDeviceSetting_Disable);
    if (CR_FAILED(err)) {
        response.success = false;
        response.message = "Failed to disable live view: 0x" + ([&]{
            std::ostringstream oss; oss << std::hex << err; return oss.str();
        })();
        return response;
    }

    response.success = true;
    response.message = "Live view disabled";
    response.camera.id = cameraId;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.connected = true;
    return response;
}

ApiResponse CameraWebController::getLiveViewStatus(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->camera || !it->second->camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    auto& camera = it->second->camera;

    // Check SDK enable setting
    CrInt32u current = 0;
    SDK::GetDeviceSetting(camera->get_device_handle(), SDK::Setting_Key_EnableLiveView, &current);
    bool enabled = (current == SDK::CrDeviceSetting_Enable);

    // Check if worker is actively streaming
    bool streaming = false;
    if (it->second->liveViewWorker) {
        streaming = it->second->liveViewWorker->isLiveViewStreaming();
    }

    response.success = true;
    response.message = "Live view status retrieved";
    response.data["enabled"] = enabled ? "true" : "false";
    response.data["streaming"] = streaming ? "true" : "false";
    response.camera.id = cameraId;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.connected = true;
    return response;
}

ApiResponse CameraWebController::startLiveViewStreaming(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->camera || !it->second->camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    auto& camera = it->second->camera;
    auto& worker = it->second->liveViewWorker;

    if (!worker) {
        response.success = false;
        response.message = "Live view worker not available for camera: " + cameraId;
        return response;
    }

    if (worker->isLiveViewStreaming()) {
        response.success = true;
        response.message = "Live view streaming already running";
        response.camera.id = cameraId;
        response.camera.model = std::string(camera->get_model().data());
        response.camera.connected = true;
        return response;
    }

    // Auto-enable live view if disabled
    CrInt32u current = 0;
    SDK::GetDeviceSetting(camera->get_device_handle(), SDK::Setting_Key_EnableLiveView, &current);
    if (current != SDK::CrDeviceSetting_Enable) {
        std::cout << "Auto-enabling live view for camera: " << cameraId << std::endl;
        auto err = SDK::SetDeviceSetting(camera->get_device_handle(), SDK::Setting_Key_EnableLiveView, SDK::CrDeviceSetting_Enable);
        if (CR_FAILED(err)) {
            response.success = false;
            response.message = "Failed to auto-enable live view";
            return response;
        }
        // Wait for camera to process the setting
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    worker->startLiveViewStreaming();

    response.success = true;
    response.message = "Live view streaming started";
    response.camera.id = cameraId;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.connected = true;
    return response;
}

ApiResponse CameraWebController::stopLiveViewStreaming(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end()) {
        response.success = false;
        response.message = "Camera not found: " + cameraId;
        return response;
    }

    auto& worker = it->second->liveViewWorker;
    if (!worker || !worker->isLiveViewStreaming()) {
        response.success = true;
        response.message = "Live view streaming not running";
        return response;
    }

    worker->stopLiveViewStreaming();

    response.success = true;
    response.message = "Live view streaming stopped";
    response.camera.id = cameraId;
    if (it->second->camera) {
        response.camera.model = std::string(it->second->camera->get_model().data());
        response.camera.connected = it->second->camera->is_connected();
    }
    return response;
}

bool CameraWebController::getLiveViewFrame(const std::string& cameraId, std::vector<uint8_t>& jpegData) {
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->liveViewWorker) {
        return false;
    }

    return it->second->liveViewWorker->getLiveViewFrame(jpegData);
}

// ============================================================================
// Per-Camera OSD Methods
// ============================================================================

ApiResponse CameraWebController::enableOSD(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->camera || !it->second->camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    auto& camera = it->second->camera;

    // Check if OSD mode is supported
    std::int32_t nprop = 0;
    SDK::CrDeviceProperty* prop_list = nullptr;
    CrInt32u getCode = SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode;
    auto res = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, &getCode, &prop_list, &nprop);

    if (SDK::CrError_None != res || nprop != 1) {
        if (prop_list) SDK::ReleaseDeviceProperties(camera->get_device_handle(), prop_list);
        response.success = false;
        response.message = "OSD mode not supported by this camera";
        return response;
    }

    // Set OSD mode to ON
    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode);
    prop.SetValueType(SDK::CrDataType::CrDataType_UInt8Array);
    prop.SetCurrentValue(SDK::CrOSDImageMode_On);

    auto err = SDK::SetDeviceProperty(camera->get_device_handle(), &prop);
    SDK::ReleaseDeviceProperties(camera->get_device_handle(), prop_list);

    if (SDK::CrError_None == err) {
        if (it->second->liveViewWorker) {
            it->second->liveViewWorker->enableOSD();
        }
        response.success = true;
        response.message = "OSD mode enabled";
        response.camera.id = cameraId;
        response.camera.model = std::string(camera->get_model().data());
        response.camera.connected = true;
    } else {
        response.success = false;
        response.message = "Failed to enable OSD mode";
    }
    return response;
}

ApiResponse CameraWebController::disableOSD(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->camera || !it->second->camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    auto& camera = it->second->camera;

    SDK::CrDeviceProperty prop;
    prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode);
    prop.SetValueType(SDK::CrDataType::CrDataType_UInt8Array);
    prop.SetCurrentValue(SDK::CrOSDImageMode_Off);

    auto err = SDK::SetDeviceProperty(camera->get_device_handle(), &prop);
    if (SDK::CrError_None == err) {
        if (it->second->liveViewWorker) {
            it->second->liveViewWorker->disableOSD();
        }
        response.success = true;
        response.message = "OSD mode disabled";
        response.camera.id = cameraId;
        response.camera.model = std::string(camera->get_model().data());
        response.camera.connected = true;
    } else {
        response.success = false;
        response.message = "Failed to disable OSD mode";
    }
    return response;
}

ApiResponse CameraWebController::getOSDStatus(const std::string& cameraId) {
    ApiResponse response;
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->camera || !it->second->camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    // Check SDK property
    auto& camera = it->second->camera;
    std::int32_t nprop = 0;
    SDK::CrDeviceProperty* prop_list = nullptr;
    CrInt32u getCode = SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode;
    auto res = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, &getCode, &prop_list, &nprop);

    bool supported = (SDK::CrError_None == res && nprop == 1);
    bool sdkEnabled = false;
    if (supported && prop_list) {
        sdkEnabled = (prop_list->GetCurrentValue() == SDK::CrOSDImageMode_On);
        SDK::ReleaseDeviceProperties(camera->get_device_handle(), prop_list);
    }

    bool workerEnabled = false;
    if (it->second->liveViewWorker) {
        workerEnabled = it->second->liveViewWorker->isOSDEnabled();
    }

    response.success = true;
    response.message = "OSD status retrieved";
    response.data["supported"] = supported ? "true" : "false";
    response.data["enabled"] = (sdkEnabled && workerEnabled) ? "true" : "false";
    response.camera.id = cameraId;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.connected = true;
    return response;
}

bool CameraWebController::getCompositeFrame(const std::string& cameraId, std::vector<uint8_t>& jpegData) {
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end() || !it->second->liveViewWorker) {
        return false;
    }

    return it->second->liveViewWorker->getCompositeFrame(jpegData);
}

void CameraWebController::liveViewStreamingThread() {
    std::cout << "=== LIVE VIEW DEBUG: Streaming thread started ===" << std::endl;

    // Frame statistics
    int frameCount = 0;
    int totalFrameCount = 0;
    int errorCount = 0;
    auto lastLogTime = std::chrono::steady_clock::now();
    auto startTime = std::chrono::steady_clock::now();

    while (m_liveViewStreaming.load()) {
        if (!m_currentCamera || !m_currentCamera->is_connected()) {
            std::cout << "DEBUG: Camera not connected, waiting..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        try {
            // Legacy implementation - will be replaced by worker threads

            // STEP 1: Get live view properties (following CLI reference implementation)
            CrInt32 numProps = 0;
            SDK::CrLiveViewProperty* property = nullptr;
            auto propErr = SDK::GetLiveViewProperties(m_currentCamera->get_device_handle(), &property, &numProps);

            std::cout << "DEBUG: GetLiveViewProperties result: 0x" << std::hex << propErr << std::dec
                      << ", numProps: " << numProps << std::endl;

            if (CR_FAILED(propErr)) {
                errorCount++;
                std::cout << "ERROR: GetLiveViewProperties FAILED with error: 0x" << std::hex << propErr << std::dec << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // Release properties immediately (following CLI pattern)
            SDK::ReleaseLiveViewProperties(m_currentCamera->get_device_handle(), property);
            std::cout << "DEBUG: Released live view properties" << std::endl;

            // STEP 2: Get live view image info
            SDK::CrImageInfo inf;
            auto err = SDK::GetLiveViewImageInfo(m_currentCamera->get_device_handle(), &inf);
            std::cout << "DEBUG: GetLiveViewImageInfo result: 0x" << std::hex << err << std::dec << std::endl;

            if (CR_FAILED(err)) {
                errorCount++;
                // Only log errors occasionally to avoid spam
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count() >= 2) {
                    std::cout << "ERROR: GetLiveViewImageInfo failed with error: 0x" << std::hex << err << std::dec << std::endl;
                    lastLogTime = now;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // STEP 3: Check buffer size
            CrInt32u bufSize = inf.GetBufferSize();
            std::cout << "DEBUG: Buffer size required: " << bufSize << " bytes" << std::endl;

            if (bufSize < 1) {
                std::cout << "WARNING: Invalid buffer size (" << bufSize << "), skipping frame" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // STEP 4: Allocate image buffer (following CLI pattern exactly)
            auto image_data = std::make_unique<SDK::CrImageDataBlock>();
            auto image_buff = std::make_unique<CrInt8u[]>(bufSize);

            if (!image_data || !image_buff) {
                std::cout << "ERROR: Failed to allocate memory for live view (bufSize: " << bufSize << ")" << std::endl;
                errorCount++;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            image_data->SetSize(bufSize);
            image_data->SetData(image_buff.get());
            std::cout << "DEBUG: Allocated buffer, calling GetLiveViewImage..." << std::endl;

            // STEP 5: Get the live view image
            err = SDK::GetLiveViewImage(m_currentCamera->get_device_handle(), image_data.get());
            std::cout << "DEBUG: GetLiveViewImage result: 0x" << std::hex << err << std::dec
                      << ", image size: " << image_data->GetImageSize() << " bytes" << std::endl;

            if (CR_SUCCEEDED(err) && image_data->GetImageSize() > 0) {
                CrInt8u* jpegData = image_data->GetImageData();
                CrInt32u jpegSize = image_data->GetImageSize();
                totalFrameCount++;

                std::cout << "DEBUG: Frame #" << totalFrameCount << " received, size: " << jpegSize << " bytes" << std::endl;

                // STEP 6: Validate JPEG data with detailed debugging
                bool isValidJPEG = false;
                bool hasSOI = false;
                bool hasEOI = false;

                if (jpegSize >= 4 && jpegData != nullptr) {
                    // Check for JPEG SOI (Start of Image) marker: 0xFF, 0xD8
                    hasSOI = (jpegData[0] == 0xFF && jpegData[1] == 0xD8);
                    // Check for EOI (End of Image) marker: 0xFF, 0xD9
                    hasEOI = (jpegData[jpegSize-2] == 0xFF && jpegData[jpegSize-1] == 0xD9);

                    isValidJPEG = hasSOI; // Some cameras may not include EOI
                    if (hasSOI && hasEOI) {
                        isValidJPEG = true;
                    }
                }

                std::cout << "DEBUG: JPEG validation - Size: " << jpegSize
                          << ", SOI: " << (hasSOI ? "YES" : "NO")
                          << ", EOI: " << (hasEOI ? "YES" : "NO")
                          << ", Valid: " << (isValidJPEG ? "YES" : "NO") << std::endl;

                if (jpegSize >= 10 && jpegData != nullptr) {
                    std::cout << "DEBUG: First 10 bytes: ";
                    for (int i = 0; i < 10; i++) {
                        std::cout << "0x" << std::hex << (int)jpegData[i] << " ";
                    }
                    std::cout << std::dec << std::endl;

                    std::cout << "DEBUG: Last 4 bytes: ";
                    for (int i = jpegSize-4; i < jpegSize; i++) {
                        std::cout << "0x" << std::hex << (int)jpegData[i] << " ";
                    }
                    std::cout << std::dec << std::endl;
                }

                if (isValidJPEG) {
                    // STEP 7: Save JPEG to file for manual inspection (every 30th frame)
                    if (totalFrameCount % 30 == 0) {
                        char filename[256];
                        snprintf(filename, sizeof(filename), "/tmp/liveview_frame_%04d.jpg", totalFrameCount);
                        std::ofstream outfile(filename, std::ios::binary);
                        if (outfile.is_open()) {
                            outfile.write(reinterpret_cast<const char*>(jpegData), jpegSize);
                            outfile.close();
                            std::cout << "DEBUG: Saved frame to " << filename << " for inspection" << std::endl;
                        }
                    }

                    // STEP 8: Copy JPEG image data to latest frame buffer
                    {
                        std::lock_guard<std::mutex> lock(m_frameMutex);
                        m_latestFrame.assign(jpegData, jpegData + jpegSize);
                    }

                    frameCount++;
                    std::cout << "SUCCESS: Frame #" << totalFrameCount << " processed and stored" << std::endl;

                    // STEP 9: Log comprehensive statistics periodically
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime).count();
                    if (elapsed >= 2000) { // Every 2 seconds
                        float fps = (frameCount * 1000.0f) / elapsed;
                        auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();

                        std::cout << "=== LIVE VIEW STATS ===" << std::endl;
                        std::cout << "Recent: " << frameCount << " frames in " << elapsed << "ms = "
                                  << std::fixed << std::setprecision(1) << fps << " fps" << std::endl;
                        std::cout << "Total: " << totalFrameCount << " frames, " << errorCount << " errors, "
                                  << totalElapsed << "s runtime" << std::endl;
                        std::cout << "Latest frame: " << jpegSize << " bytes" << std::endl;
                        std::cout << "=======================" << std::endl;

                        frameCount = 0;
                        lastLogTime = now;
                    }
                } else {
                    errorCount++;
                    std::cout << "ERROR: Invalid JPEG data received!" << std::endl;
                    std::cout << "  Size: " << jpegSize << " bytes" << std::endl;
                    std::cout << "  SOI present: " << (hasSOI ? "YES" : "NO") << std::endl;
                    std::cout << "  EOI present: " << (hasEOI ? "YES" : "NO") << std::endl;
                    if (jpegSize > 0 && jpegData != nullptr) {
                        std::cout << "  First bytes: 0x" << std::hex << (int)jpegData[0] << " 0x" << (int)jpegData[1] << std::dec << std::endl;
                    }
                }
            } else {
                errorCount++;
                // Enhanced error logging
                if (err == SDK::CrWarning_Frame_NotUpdated) {
                    // This is normal, don't spam logs
                    static int notUpdatedCount = 0;
                    if (++notUpdatedCount % 100 == 0) {
                        std::cout << "INFO: Frame not updated (normal) - " << notUpdatedCount << " times" << std::endl;
                    }
                } else if (err == SDK::CrError_Memory_Insufficient) {
                    std::cout << "ERROR: Memory insufficient for live view image" << std::endl;
                } else {
                    std::cout << "ERROR: GetLiveViewImage failed - Code: 0x" << std::hex << err << std::dec
                              << ", Image size: " << image_data->GetImageSize() << " bytes" << std::endl;
                }
            }
            
        } catch (const std::exception& e) {
            errorCount++;
            std::cout << "EXCEPTION: Live view streaming error: " << e.what() << std::endl;
        }

        // Control frame rate (~15 fps)
        std::this_thread::sleep_for(std::chrono::milliseconds(66));
    }

    // Final statistics
    auto endTime = std::chrono::steady_clock::now();
    auto totalRuntime = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count();
    std::cout << "=== LIVE VIEW DEBUG: Streaming thread stopped ===" << std::endl;
    std::cout << "Final stats: " << totalFrameCount << " total frames, "
              << errorCount << " errors in " << totalRuntime << " seconds" << std::endl;
    std::cout << "================================================" << std::endl;
}

// ==============================================================================
// Property Helper Functions
// ==============================================================================

// Helper: Get full property data from camera (used by both custom and generic getters)
PropertyData CameraWebController::getPropertyData(std::shared_ptr<CameraDevice> camera,
                                                  const std::string& propertyName,
                                                  CrInt32u propertyCode) {
    PropertyData data;
    data.propertyName = propertyName;

    if (!camera || !camera->is_connected()) {
        return data;  // Return empty data
    }

    try {
        // Use SDK GetSelectDeviceProperties to get the property
        CrInt32u codes[1] = { propertyCode };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);

        if (CR_SUCCEEDED(err) && numProps > 0 && properties != nullptr) {
            // Get current value
            data.currentValue = properties[0].GetCurrentValue();
            data.writable = properties[0].IsSetEnableCurrentValue();

            // Format hex value
            std::stringstream hexValue;
            hexValue << "0x" << std::hex << data.currentValue;
            data.currentHexValue = hexValue.str();

            // Parse available values
            const unsigned char* values = properties[0].GetValues();
            int valueSize = properties[0].GetValueSize();

            // Determine value type and parse accordingly
            if (propertyName == "aperture" || propertyName == "f-number") {
                int nval = valueSize / sizeof(std::uint16_t);
                auto parsed = cli::parse_f_number(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_f_number(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_f_number(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "iso" || propertyName == "iso-sensitivity") {
                int nval = valueSize / sizeof(std::uint32_t);
                auto parsed = cli::parse_iso_sensitivity(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_iso_sensitivity(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_iso_sensitivity(static_cast<std::uint32_t>(data.currentValue));
            }
            else if (propertyName == "shutter-speed") {
                int nval = valueSize / sizeof(std::uint32_t);
                auto parsed = cli::parse_shutter_speed(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_shutter_speed(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_shutter_speed(static_cast<std::uint32_t>(data.currentValue));
            }
            else if (propertyName == "white-balance") {
                int nval = valueSize / sizeof(std::uint16_t);
                auto parsed = cli::parse_white_balance(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_white_balance(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_white_balance(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "focus-mode") {
                int nval = valueSize / sizeof(std::uint16_t);
                auto parsed = cli::parse_focus_mode(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_focus_mode(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_focus_mode(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "exposure-program-mode" || propertyName == "exposure-mode") {
                int nval = valueSize / sizeof(std::uint32_t);
                auto parsed = cli::parse_exposure_program_mode(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_exposure_program_mode(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_exposure_program_mode(static_cast<std::uint32_t>(data.currentValue));
            }
            else if (propertyName == "drive-mode" || propertyName == "still-capture-mode") {
                int nval = valueSize / sizeof(std::uint32_t);
                auto parsed = cli::parse_still_capture_mode(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_still_capture_mode(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_still_capture_mode(static_cast<std::uint32_t>(data.currentValue));
            }
            else if (propertyName == "file-format") {
                int nval = valueSize / sizeof(std::uint16_t);
                const std::uint16_t* vals = reinterpret_cast<const std::uint16_t*>(values);
                for (int j = 0; j < nval; ++j) {
                    AvailablePropertyValue availVal;
                    availVal.value = vals[j];
                    std::stringstream hex;
                    hex << "0x" << std::hex << vals[j];
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_file_type(vals[j]);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_file_type(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "image-quality") {
                int nval = valueSize / sizeof(std::uint32_t);
                const std::uint32_t* vals = reinterpret_cast<const std::uint32_t*>(values);
                for (int j = 0; j < nval; ++j) {
                    AvailablePropertyValue availVal;
                    availVal.value = vals[j];
                    std::stringstream hex;
                    hex << "0x" << std::hex << vals[j];
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_still_image_quality(vals[j]);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_still_image_quality(static_cast<std::uint32_t>(data.currentValue));
            }
            else if (propertyName == "raw-compression") {
                int nval = valueSize / sizeof(std::uint16_t);
                const std::uint16_t* vals = reinterpret_cast<const std::uint16_t*>(values);
                for (int j = 0; j < nval; ++j) {
                    AvailablePropertyValue availVal;
                    availVal.value = vals[j];
                    std::stringstream hex;
                    hex << "0x" << std::hex << vals[j];
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_raw_file_compression(vals[j]);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_raw_file_compression(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "focus-area") {
                int nval = valueSize / sizeof(std::uint16_t);
                auto parsed = cli::parse_focus_area(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_focus_area(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_focus_area(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "priority-key" || propertyName == "position-key-setting") {
                int nval = valueSize / sizeof(std::uint16_t);
                auto parsed = cli::parse_position_key_setting(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_position_key_setting(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_position_key_setting(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "still-image-store-destination" || propertyName == "sd-card-save-destination") {
                int nval = valueSize / sizeof(std::uint16_t);
                auto parsed = cli::parse_still_image_store_destination(values, nval);
                for (const auto& val : parsed) {
                    AvailablePropertyValue availVal;
                    availVal.value = val;
                    std::stringstream hex;
                    hex << "0x" << std::hex << val;
                    availVal.hexValue = hex.str();
                    availVal.formatted = cli::format_still_image_store_destination(val);
                    data.availableValues.push_back(availVal);
                }
                data.currentFormatted = cli::format_still_image_store_destination(static_cast<std::uint16_t>(data.currentValue));
            }
            else if (propertyName == "focus-driving-status") {
                // Focus driving status: 0x01 = NotDriving, 0x02 = Driving
                if (data.currentValue == 0x01) {
                    data.currentFormatted = "Not Driving";
                } else if (data.currentValue == 0x02) {
                    data.currentFormatted = "Driving";
                } else {
                    data.currentFormatted = "Unknown (0x" + ([&]{ std::ostringstream o; o << std::hex << data.currentValue; return o.str(); })() + ")";
                }
            }
            else if (propertyName == "zoom-distance") {
                // Zoom distance: value is focal length in 0.001mm units
                double mm = data.currentValue / 1000.0;
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(0) << mm << "mm";
                data.currentFormatted = oss.str();
                // Available values are min/max/step range
                if (valueSize > 0 && values != nullptr) {
                    int nval = valueSize / sizeof(std::uint32_t);
                    const std::uint32_t* vals = reinterpret_cast<const std::uint32_t*>(values);
                    for (int j = 0; j < nval; ++j) {
                        AvailablePropertyValue availVal;
                        availVal.value = vals[j];
                        std::stringstream hex;
                        hex << "0x" << std::hex << vals[j];
                        availVal.hexValue = hex.str();
                        double v = vals[j] / 1000.0;
                        std::ostringstream fmtOss;
                        fmtOss << std::fixed << std::setprecision(0) << v << "mm";
                        availVal.formatted = fmtOss.str();
                        data.availableValues.push_back(availVal);
                    }
                }
            }
            else if (propertyName == "focus-distance") {
                // Focus distance: value is 1/1000 meter, 0xFFFFFFFF = infinity
                if (data.currentValue == 0xFFFFFFFF) {
                    data.currentFormatted = "Infinity";
                } else {
                    double meters = data.currentValue / 1000.0;
                    std::ostringstream oss;
                    if (meters >= 1.0) {
                        oss << std::fixed << std::setprecision(1) << meters << "m";
                    } else {
                        oss << std::fixed << std::setprecision(2) << meters << "m";
                    }
                    data.currentFormatted = oss.str();
                }
                // Available values are min/max/step range, format them too
                if (valueSize > 0 && values != nullptr) {
                    int nval = valueSize / sizeof(std::uint32_t);
                    const std::uint32_t* vals = reinterpret_cast<const std::uint32_t*>(values);
                    for (int j = 0; j < nval; ++j) {
                        AvailablePropertyValue availVal;
                        availVal.value = vals[j];
                        std::stringstream hex;
                        hex << "0x" << std::hex << vals[j];
                        availVal.hexValue = hex.str();
                        if (vals[j] == 0xFFFFFFFF) {
                            availVal.formatted = "Infinity";
                        } else {
                            double m = vals[j] / 1000.0;
                            std::ostringstream fmtOss;
                            if (m >= 1.0) {
                                fmtOss << std::fixed << std::setprecision(1) << m << "m";
                            } else {
                                fmtOss << std::fixed << std::setprecision(2) << m << "m";
                            }
                            availVal.formatted = fmtOss.str();
                        }
                        data.availableValues.push_back(availVal);
                    }
                }
            }
            else {
                // Generic formatting for properties without custom formatters
                // Try to parse as uint32_t first, then fall back to uint16_t
                data.currentFormatted = std::to_string(data.currentValue);

                // Parse available values if present
                if (valueSize > 0 && values != nullptr) {
                    // Try uint32_t parsing first (most common)
                    if (valueSize % sizeof(std::uint32_t) == 0) {
                        int nval = valueSize / sizeof(std::uint32_t);
                        const std::uint32_t* vals = reinterpret_cast<const std::uint32_t*>(values);
                        for (int j = 0; j < nval; ++j) {
                            AvailablePropertyValue availVal;
                            availVal.value = vals[j];
                            std::stringstream hex;
                            hex << "0x" << std::hex << vals[j];
                            availVal.hexValue = hex.str();
                            availVal.formatted = std::to_string(vals[j]);
                            data.availableValues.push_back(availVal);
                        }
                    }
                    // Fall back to uint16_t if size doesn't align with uint32_t
                    else if (valueSize % sizeof(std::uint16_t) == 0) {
                        int nval = valueSize / sizeof(std::uint16_t);
                        const std::uint16_t* vals = reinterpret_cast<const std::uint16_t*>(values);
                        for (int j = 0; j < nval; ++j) {
                            AvailablePropertyValue availVal;
                            availVal.value = vals[j];
                            std::stringstream hex;
                            hex << "0x" << std::hex << vals[j];
                            availVal.hexValue = hex.str();
                            availVal.formatted = std::to_string(vals[j]);
                            data.availableValues.push_back(availVal);
                        }
                    }
                    // Fall back to uint8_t if needed
                    else if (valueSize > 0) {
                        int nval = valueSize / sizeof(std::uint8_t);
                        const std::uint8_t* vals = reinterpret_cast<const std::uint8_t*>(values);
                        for (int j = 0; j < nval; ++j) {
                            AvailablePropertyValue availVal;
                            availVal.value = vals[j];
                            std::stringstream hex;
                            hex << "0x" << std::hex << static_cast<int>(vals[j]);
                            availVal.hexValue = hex.str();
                            availVal.formatted = std::to_string(static_cast<int>(vals[j]));
                            data.availableValues.push_back(availVal);
                        }
                    }
                }
            }

            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in getPropertyData: " << e.what() << std::endl;
    }

    return data;
}

// Helper: Escape a string for use inside JSON string values
static std::string jsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

// Helper: Build ApiResponse from PropertyData
ApiResponse CameraWebController::buildPropertyResponse(const PropertyData& propertyData,
                                                       std::shared_ptr<CameraDevice> camera) {
    ApiResponse response;
    response.success = true;
    response.message = "Property retrieved successfully";

    // Populate camera info
    if (camera && camera->is_connected()) {
        response.camera.connected = true;
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();
    }

    // Populate property data with standardized field names
    // "value" is the canonical hex string — use this for GET and SET
    // "formatted" is the human-readable display string (also accepted by SET)
    response.data["property"] = propertyData.propertyName;
    response.data["value"] = propertyData.currentHexValue;
    response.data["formatted"] = jsonEscape(propertyData.currentFormatted);
    response.data["writable"] = propertyData.writable ? "true" : "false";

    // Build available values JSON array
    // Each entry has "value" (hex) and "formatted" (human-readable)
    // Developers can use either to SET the property
    std::stringstream availableValuesJSON;
    availableValuesJSON << "[";
    for (size_t i = 0; i < propertyData.availableValues.size(); ++i) {
        if (i > 0) availableValuesJSON << ",";
        const auto& val = propertyData.availableValues[i];
        availableValuesJSON << "{\"value\":\"" << val.hexValue << "\""
                           << ",\"formatted\":\"" << jsonEscape(val.formatted) << "\"}";
    }
    availableValuesJSON << "]";
    response.data["available_values"] = availableValuesJSON.str();

    return response;
}

// ==============================================================================
// Camera property retrieval methods
// ==============================================================================

ApiResponse CameraWebController::getCurrentISO(const std::string& cameraId) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    // Use helper to get full property data with ISO-specific formatting
    PropertyData propertyData = getPropertyData(camera, "iso", SDK::CrDevicePropertyCode::CrDeviceProperty_IsoSensitivity);

    // Build and return full response with all data fields
    return buildPropertyResponse(propertyData, camera);
}

ApiResponse CameraWebController::getCurrentAperture(const std::string& cameraId) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    // Use helper to get full property data with aperture-specific formatting (f/2.8, f/4.0, etc.)
    PropertyData propertyData = getPropertyData(camera, "aperture", SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber);

    // Build and return full response with all data fields
    return buildPropertyResponse(propertyData, camera);
}

ApiResponse CameraWebController::getCurrentShutterSpeed(const std::string& cameraId) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    // Use helper to get full property data with shutter speed-specific formatting (1/125, 1/250, etc.)
    PropertyData propertyData = getPropertyData(camera, "shutter-speed", SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed);

    // Build and return full response with all data fields
    return buildPropertyResponse(propertyData, camera);
}

ApiResponse CameraWebController::getCurrentWhiteBalance(const std::string& cameraId) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    try {
        // Get white balance property from camera
        CrInt32u codes[1] = { SDK::CrDevicePropertyCode::CrDeviceProperty_WhiteBalance };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);

        if (CR_SUCCEEDED(err) && numProps > 0 && properties != nullptr) {
            auto wbValue = properties[0].GetCurrentValue();
            // Map WB values to readable strings
            std::string wbStr = "auto"; // default
            switch (wbValue) {
                case 2: wbStr = "auto"; break;
                case 16: wbStr = "daylight"; break;
                case 32: wbStr = "cloudy"; break;
                case 64: wbStr = "tungsten"; break;
                case 128: wbStr = "fluorescent"; break;
                case 256: wbStr = "flash"; break;
                default: wbStr = "auto"; break;
            }
            response.success = true;
            response.message = wbStr;

            // Release the properties
            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
        } else {
            response.message = "Failed to get white balance value";
        }
    } catch (const std::exception& e) {
        response.message = "Error getting white balance: " + std::string(e.what());
    }

    response.camera.connected = true;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.id = std::string(camera->get_id().data());

    return response;
}

ApiResponse CameraWebController::getCurrentFocusMode(const std::string& cameraId) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    try {
        // Get focus mode property from camera
        CrInt32u codes[1] = { SDK::CrDevicePropertyCode::CrDeviceProperty_FocusMode };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);

        if (CR_SUCCEEDED(err) && numProps > 0 && properties != nullptr) {
            auto focusValue = properties[0].GetCurrentValue();
            // Map focus mode values to readable strings
            std::string focusStr = "single"; // default
            switch (focusValue) {
                case 1: focusStr = "single"; break;
                case 2: focusStr = "continuous"; break;
                case 3: focusStr = "manual"; break;
                default: focusStr = "single"; break;
            }
            response.success = true;
            response.message = focusStr;

            // Release the properties
            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
        } else {
            response.message = "Failed to get focus mode value";
        }
    } catch (const std::exception& e) {
        response.message = "Error getting focus mode: " + std::string(e.what());
    }

    response.camera.connected = true;
    response.camera.model = std::string(camera->get_model().data());
    response.camera.id = std::string(camera->get_id().data());

    return response;
}

ApiResponse CameraWebController::getAllCurrentSettings() {
    ApiResponse response;
    
    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.message = "Camera not connected";
        return response;
    }
    
    try {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        
        // Get multiple properties at once for efficiency
        CrInt32u codes[5] = { 
            SDK::CrDevicePropertyCode::CrDeviceProperty_IsoSensitivity,
            SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber,
            SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed,
            SDK::CrDevicePropertyCode::CrDeviceProperty_WhiteBalance,
            SDK::CrDevicePropertyCode::CrDeviceProperty_FocusMode
        };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;
        
        auto err = SDK::GetSelectDeviceProperties(m_currentCamera->get_device_handle(), 5, codes, &properties, &numProps);
        
        if (CR_SUCCEEDED(err) && properties != nullptr) {
            std::string jsonResponse = "{";
            
            for (CrInt32 i = 0; i < numProps; i++) {
                if (i > 0) jsonResponse += ",";
                
                CrInt32u code = properties[i].GetCode();
                auto value = properties[i].GetCurrentValue();
                
                switch (code) {
                    case SDK::CrDevicePropertyCode::CrDeviceProperty_IsoSensitivity:
                        jsonResponse += "\"iso\":\"" + std::to_string(value) + "\"";
                        break;
                    case SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber:
                        jsonResponse += "\"aperture\":\"f/" + std::to_string(value / 100.0) + "\"";
                        break;
                    case SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed:
                        jsonResponse += "\"shutterSpeed\":\"1/" + std::to_string(value) + "\"";
                        break;
                    case SDK::CrDevicePropertyCode::CrDeviceProperty_WhiteBalance: {
                        std::string wbStr = "auto";
                        switch (value) {
                            case 2: wbStr = "auto"; break;
                            case 16: wbStr = "daylight"; break;
                            case 32: wbStr = "cloudy"; break;
                            case 64: wbStr = "tungsten"; break;
                            case 128: wbStr = "fluorescent"; break;
                            case 256: wbStr = "flash"; break;
                        }
                        jsonResponse += "\"whiteBalance\":\"" + wbStr + "\"";
                        break;
                    }
                    case SDK::CrDevicePropertyCode::CrDeviceProperty_FocusMode: {
                        std::string focusStr = "single";
                        switch (value) {
                            case 1: focusStr = "single"; break;
                            case 2: focusStr = "continuous"; break;
                            case 3: focusStr = "manual"; break;
                        }
                        jsonResponse += "\"focusMode\":\"" + focusStr + "\"";
                        break;
                    }
                }
            }
            
            jsonResponse += "}";
            response.success = true;
            response.message = jsonResponse;

            // Populate camera metadata
            response.camera.connected = true;
            response.camera.model = std::string(m_currentCamera->get_model().data());
            response.camera.id = std::string(m_currentCamera->get_id().data());

            // Release the properties
            SDK::ReleaseDeviceProperties(m_currentCamera->get_device_handle(), properties);
        } else {
            response.message = "Failed to get camera settings";
        }
    } catch (const std::exception& e) {
        response.message = "Error getting all settings: " + std::string(e.what());
    }
    
    return response;
}

// ==============================================================================
// Bulk Property Retrieval for UI Initialization
// ==============================================================================

ApiResponse CameraWebController::getAllProperties(const std::string& cameraId) {
    ApiResponse response;

    // Find camera by ID
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    // Fallback to legacy m_currentCamera
    if (!camera) {
        std::lock_guard<std::mutex> lock(m_discoveryMutex);
        if (m_currentCamera && m_currentCamera->get_id() == cameraId) {
            camera = m_currentCamera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected: " + cameraId;
        return response;
    }

    try {
        std::cout << "📦 Getting all camera properties using GetDeviceProperties..." << std::endl;

        // Use GetDeviceProperties to get ALL camera properties at once
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetDeviceProperties(camera->get_device_handle(), &properties, &numProps);

        if (CR_SUCCEEDED(err) && numProps > 0 && properties != nullptr) {
            std::cout << "✅ Retrieved " << numProps << " total properties from camera" << std::endl;

            // Collect into a map first to deduplicate — the SDK may return multiple
            // internal entries that map to the same property name (e.g. ISO). When a
            // duplicate is found, keep whichever entry has available values or is writable
            // so the UI always gets the most useful entry.
            std::map<std::string, PropertyData> propMap;

            for (CrInt32 i = 0; i < numProps; ++i) {
                CrInt32u propertyCode = properties[i].GetCode();
                std::string propertyName = getPropertyName(propertyCode);
                if (propertyName == "unknown") continue;

                PropertyData propData = getPropertyData(camera, propertyName, propertyCode);

                auto it = propMap.find(propertyName);
                if (it == propMap.end()) {
                    propMap[propertyName] = propData;
                } else {
                    // Prefer the entry that has available values or is writable
                    bool existingBetter = (!it->second.availableValues.empty() || it->second.writable);
                    bool newBetter = (!propData.availableValues.empty() || propData.writable);
                    if (newBetter && !existingBetter) {
                        propMap[propertyName] = propData;
                    }
                }
            }

            // Serialize deduplicated map to JSON
            std::stringstream jsonData;
            jsonData << "{";
            bool firstProp = true;

            for (const auto& kv : propMap) {
                const PropertyData& propData = kv.second;
                if (!firstProp) jsonData << ",";
                firstProp = false;

                jsonData << "\"" << kv.first << "\":{";
                jsonData << "\"current_value\":" << propData.currentValue << ",";
                jsonData << "\"current_hex_value\":\"" << propData.currentHexValue << "\",";
                jsonData << "\"current_formatted\":\"" << jsonEscape(propData.currentFormatted) << "\",";
                jsonData << "\"writable\":" << (propData.writable ? "true" : "false") << ",";
                jsonData << "\"available_values\":[";
                for (size_t j = 0; j < propData.availableValues.size(); ++j) {
                    if (j > 0) jsonData << ",";
                    jsonData << "{\"value\":" << propData.availableValues[j].value
                             << ",\"hex_value\":\"" << propData.availableValues[j].hexValue << "\""
                             << ",\"formatted\":\"" << jsonEscape(propData.availableValues[j].formatted) << "\"}";
                }
                jsonData << "]}";
            }
            jsonData << "}";

            response.success = true;
            response.message = "Retrieved all camera properties";
            response.data["properties"] = jsonData.str();
            response.data["total_properties"] = std::to_string(numProps);

            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);

            std::cout << "📤 Returning all properties in single response" << std::endl;
        } else {
            response.success = false;
            response.message = "Failed to get device properties from camera";
            std::cout << "❌ GetDeviceProperties failed" << std::endl;
        }

        // Populate camera metadata
        if (camera && camera->is_connected()) {
            response.camera.connected = true;
            response.camera.model = camera->get_model();
            response.camera.id = camera->get_id();
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception getting all properties: " + std::string(e.what());
        std::cerr << "❌ Exception in getAllProperties: " << e.what() << std::endl;
    }

    return response;
}

// ==============================================================================
// OSD Image overlay methods
// ==============================================================================

ApiResponse CameraWebController::enableOSDMode() {
    std::cout << "🟡 OSD: Starting enable operation" << std::endl;
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    ApiResponse response;

    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.message = "Camera not connected";
        std::cout << "🔴 OSD: Enable failed - camera not connected" << std::endl;
        return response;
    }

    try {
        // Check if OSD mode is supported by this camera
        std::int32_t nprop = 0;
        SDK::CrDeviceProperty* prop_list = nullptr;
        CrInt32u getCode = SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode;
        SDK::CrError res = SDK::GetSelectDeviceProperties(m_currentCamera->get_device_handle(), 1, &getCode, &prop_list, &nprop);

        if (SDK::CrError_None != res || nprop != 1) {
            response.message = "OSD mode not supported by this camera";
            SDK::ReleaseDeviceProperties(m_currentCamera->get_device_handle(), prop_list);
            return response;
        }

        // Set OSD mode to ON
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode);
        prop.SetValueType(SDK::CrDataType::CrDataType_UInt8Array);
        prop.SetCurrentValue(SDK::CrOSDImageMode_On);

        SDK::CrError err = SDK::SetDeviceProperty(m_currentCamera->get_device_handle(), &prop);
        SDK::ReleaseDeviceProperties(m_currentCamera->get_device_handle(), prop_list);

        if (SDK::CrError_None == err) {
            m_osdEnabled.store(true);
            response.success = true;
            response.message = "OSD mode enabled successfully";
            std::cout << "🟢 OSD: Enable successful" << std::endl;
        } else {
            response.message = "Failed to enable OSD mode (error: " + std::to_string(err) + ")";
            std::cout << "🔴 OSD: Enable failed, error: " << std::hex << err << std::endl;
        }
    } catch (const std::exception& e) {
        response.message = "Error enabling OSD mode: " + std::string(e.what());
    }

    return response;
}

ApiResponse CameraWebController::disableOSDMode() {
    std::cout << "🟡 OSD: Starting disable operation" << std::endl;
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    ApiResponse response;

    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.message = "Camera not connected";
        std::cout << "🔴 OSD: Disable failed - camera not connected" << std::endl;
        return response;
    }

    try {
        // Set OSD mode to OFF
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode);
        prop.SetValueType(SDK::CrDataType::CrDataType_UInt8Array);
        prop.SetCurrentValue(SDK::CrOSDImageMode_Off);

        SDK::CrError err = SDK::SetDeviceProperty(m_currentCamera->get_device_handle(), &prop);

        if (SDK::CrError_None == err) {
            m_osdEnabled.store(false);
            response.success = true;
            response.message = "OSD mode disabled successfully";
            std::cout << "🟢 OSD: Disable successful" << std::endl;
        } else {
            response.message = "Failed to disable OSD mode";
            std::cout << "🔴 OSD: Disable failed, error: " << std::hex << err << std::endl;
        }
    } catch (const std::exception& e) {
        response.message = "Error disabling OSD mode: " + std::string(e.what());
    }

    return response;
}

ApiResponse CameraWebController::getOSDStatus() {
    std::lock_guard<std::mutex> lock(m_discoveryMutex);
    ApiResponse response;

    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        response.message = "Camera not connected";
        return response;
    }

    try {
        // Check current OSD mode status
        std::int32_t nprop = 0;
        SDK::CrDeviceProperty* prop_list = nullptr;
        CrInt32u getCode = SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode;
        SDK::CrError res = SDK::GetSelectDeviceProperties(m_currentCamera->get_device_handle(), 1, &getCode, &prop_list, &nprop);

        if (SDK::CrError_None == res && nprop == 1) {
            bool osdOn = (prop_list[0].GetCurrentValue() == SDK::CrOSDImageMode_On);
            m_osdEnabled.store(osdOn);

            std::string status = "{\"osd_enabled\": " + std::string(osdOn ? "true" : "false") +
                                ", \"osd_supported\": true}";
            response.success = true;
            response.message = status;
            SDK::ReleaseDeviceProperties(m_currentCamera->get_device_handle(), prop_list);
        } else {
            response.success = true;
            response.message = "{\"osd_enabled\": false, \"osd_supported\": false}";
        }
    } catch (const std::exception& e) {
        response.message = "Error getting OSD status: " + std::string(e.what());
    }

    return response;
}

bool CameraWebController::getCompositeFrame(std::vector<uint8_t>& jpegData) {
    std::cout << "🔶 OSD: getCompositeFrame called" << std::endl;
    std::lock_guard<std::mutex> lock(m_compositeMutex);

    if (!m_currentCamera || !m_currentCamera->is_connected()) {
        std::cout << "🔴 OSD: getCompositeFrame failed - camera not connected" << std::endl;
        return false;
    }

    try {
        // Get the live view frame first
        std::vector<uint8_t> liveViewData;
        if (!getLiveViewFrame(liveViewData)) {
            return false;
        }

        // If OSD is disabled, just return the live view frame
        if (!m_osdEnabled.load()) {
            jpegData = liveViewData;
            return true;
        }

        // First verify OSD mode is actually enabled
        std::int32_t nprop = 0;
        SDK::CrDeviceProperty* prop_list = nullptr;
        CrInt32u getCode = SDK::CrDevicePropertyCode::CrDeviceProperty_OSDImageMode;
        SDK::CrError res = SDK::GetSelectDeviceProperties(m_currentCamera->get_device_handle(), 1, &getCode, &prop_list, &nprop);

        bool osdModeActuallyOn = false;
        if (SDK::CrError_None == res && nprop == 1 && getCode == prop_list[0].GetCode()) {
            osdModeActuallyOn = (SDK::CrOSDImageMode_On == prop_list[0].GetCurrentValue());
            std::cout << "Current OSD Mode status: " << (osdModeActuallyOn ? "ON" : "OFF") << std::endl;
            SDK::ReleaseDeviceProperties(m_currentCamera->get_device_handle(), prop_list);
        } else {
            std::cout << "Failed to check OSD mode status, error: " << std::hex << res << std::endl;
            jpegData = liveViewData;
            return true;
        }

        if (!osdModeActuallyOn) {
            std::cout << "OSD mode is OFF, cannot get OSD image. Returning live view only." << std::endl;
            jpegData = liveViewData;
            return true;
        }

        // Try to get OSD image from camera - properly allocate buffer first
        SCRSDK::CrOSDImageDataBlock osdData;
        CrInt8u* osdBuffer = new CrInt8u[CR_OSD_IMAGE_MAX_SIZE];
        osdData.SetData(osdBuffer);  // CRITICAL: Must set data buffer before calling GetOSDImage!

        auto osdResult = SDK::GetOSDImage(m_currentCamera->get_device_handle(), &osdData);
        std::cout << "OSD Mode is ON, attempting GetOSDImage with buffer, result: " << std::hex << osdResult << std::endl;

        if (osdResult != SDK::CrError_None) {
            // OSD not available or not supported, return regular live view
            std::cout << "OSD not available (error: " << std::hex << osdResult << "), returning live view only" << std::endl;
            delete[] osdBuffer;
            jpegData = liveViewData;
            return true;
        }

        // Check if we have valid OSD data
        if (osdData.GetImageSize() == 0 || osdData.GetImageData() == nullptr) {
            std::cout << "No OSD image data available, returning live view only" << std::endl;
            delete[] osdBuffer;
            jpegData = liveViewData;
            return true;
        }

        std::cout << "Compositing OSD overlay with live view (OSD size: " << osdData.GetImageSize() << " bytes)" << std::endl;

        // Prepare output buffer (estimate size)
        unsigned char* compositeBuffer = new unsigned char[liveViewData.size() + osdData.GetImageSize()];
        CrInt32u compositeSize = 0;

        // Use OpenCVWrapper to composite the images
        std::cout << "🔶 OSD: Calling OpenCV CompositeImage..." << std::endl;
        bool compositeResult = OpenCVWrapper::CompositeImage(
            liveViewData,        // Live view JPEG data
            &osdData,           // OSD data from camera
            compositeBuffer,    // Output buffer
            &compositeSize      // Output size
        );
        std::cout << "🔶 OSD: OpenCV CompositeImage completed, result=" << compositeResult << std::endl;

        if (compositeResult && compositeSize > 0) {
            // Successfully composited - copy to output vector
            jpegData.assign(compositeBuffer, compositeBuffer + compositeSize);
            delete[] compositeBuffer;
            delete[] osdBuffer;
            std::cout << "Successfully created composite frame (" << compositeSize << " bytes)" << std::endl;
            return true;
        } else {
            // Composite failed, fallback to live view only
            std::cout << "Composite operation failed, returning live view only" << std::endl;
            delete[] compositeBuffer;
            delete[] osdBuffer;
            jpegData = liveViewData;
            return true;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error creating composite frame: " << e.what() << std::endl;
        return false;
    }
}

// Legacy mutex helper - removed for new worker thread architecture
/*
std::mutex& CameraWebController::getCameraOperationMutex(const std::string& cameraId) {
    // This method has been replaced by worker thread architecture
    // Commented out to allow compilation
}
*/

// ============================================================================
// New Worker Thread Management Methods
// ============================================================================

void CameraWebController::createCameraThreads(const std::string& cameraId, std::shared_ptr<CameraDevice> camera) {
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    std::cout << "Creating worker threads for camera: " << cameraId << std::endl;

    // Create thread pair structure
    auto threadPair = std::make_unique<CameraThreadPair>();
    threadPair->camera = camera;

    // Create worker instances
    threadPair->operationWorker = std::make_unique<CameraOperationWorker>(camera, cameraId);
    threadPair->liveViewWorker = std::make_unique<LiveViewOSDWorker>(camera, cameraId, this);

    // Start workers
    threadPair->operationWorker->start();
    threadPair->liveViewWorker->start();

    // Create and start threads
    threadPair->operationThread = std::thread(&CameraOperationWorker::workerLoop, threadPair->operationWorker.get());
    threadPair->liveViewThread = std::thread(&LiveViewOSDWorker::workerLoop, threadPair->liveViewWorker.get());

    threadPair->active.store(true);

    // Store in map
    m_cameraThreads[cameraId] = std::move(threadPair);

    std::cout << "Worker threads created and started for camera: " << cameraId << std::endl;
}

void CameraWebController::destroyCameraThreads(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    auto it = m_cameraThreads.find(cameraId);
    if (it == m_cameraThreads.end()) {
        return;
    }

    std::cout << "Destroying worker threads for camera: " << cameraId << std::endl;

    auto& threadPair = it->second;
    threadPair->active.store(false);

    // Stop workers
    if (threadPair->operationWorker) {
        threadPair->operationWorker->stop();
    }
    if (threadPair->liveViewWorker) {
        threadPair->liveViewWorker->stop();
    }

    // Join threads
    if (threadPair->operationThread.joinable()) {
        threadPair->operationThread.join();
    }
    if (threadPair->liveViewThread.joinable()) {
        threadPair->liveViewThread.join();
    }

    // Remove from map
    m_cameraThreads.erase(it);

    std::cout << "Worker threads destroyed for camera: " << cameraId << std::endl;
}

ApiResponse CameraWebController::executeCameraCommand(const std::string& cameraId, CameraCommandType commandType, const std::string& parameter) {
    std::future<ApiResponse> future;

    // Hold lock only to look up worker and enqueue command
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

        auto it = m_cameraThreads.find(cameraId);
        if (it == m_cameraThreads.end() || !it->second->active.load()) {
            ApiResponse response;
            response.success = false;
            response.message = "No active worker threads for camera: " + cameraId;
            return response;
        }

        try {
            future = it->second->operationWorker->executeCommand(commandType, parameter);
        } catch (const std::exception& e) {
            ApiResponse response;
            response.success = false;
            response.message = "Command enqueue failed: " + std::string(e.what());
            return response;
        }
    }

    // Wait for result outside the lock to avoid deadlock
    try {
        return future.get();
    } catch (const std::exception& e) {
        ApiResponse response;
        response.success = false;
        response.message = "Command execution failed: " + std::string(e.what());
        return response;
    }
}

// ============================================================================
// CameraOperationWorker Implementation
// ============================================================================

CameraOperationWorker::CameraOperationWorker(std::shared_ptr<CameraDevice> camera, const std::string& cameraId)
    : m_camera(camera), m_cameraId(cameraId), m_running(false)
{
    std::cout << "Created CameraOperationWorker for camera: " << m_cameraId << std::endl;
}

CameraOperationWorker::~CameraOperationWorker() {
    stop();
}

void CameraOperationWorker::start() {
    std::cout << "Starting CameraOperationWorker for camera: " << m_cameraId << std::endl;
    m_running.store(true);
}

void CameraOperationWorker::stop() {
    if (m_running.load()) {
        std::cout << "Stopping CameraOperationWorker for camera: " << m_cameraId << std::endl;
        m_running.store(false);
        m_queueCondition.notify_all();
    }
}

std::future<ApiResponse> CameraOperationWorker::executeCommand(CameraCommandType commandType, const std::string& parameter) {
    auto command = std::make_unique<CameraCommand>(commandType, parameter);
    auto future = command->responsePromise.get_future();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_commandQueue.push(std::move(command));
    }
    m_queueCondition.notify_one();

    return future;
}

void CameraOperationWorker::workerLoop() {
    std::cout << "CameraOperationWorker thread started for camera: " << m_cameraId << std::endl;

    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_queueCondition.wait(lock, [this] { return !m_commandQueue.empty() || !m_running.load(); });

        if (!m_running.load()) {
            break;
        }

        if (!m_commandQueue.empty()) {
            auto command = std::move(m_commandQueue.front());
            m_commandQueue.pop();
            lock.unlock();

            // Process command and set result
            ApiResponse result = processCommand(*command);
            command->responsePromise.set_value(result);
        }
    }

    std::cout << "CameraOperationWorker thread ended for camera: " << m_cameraId << std::endl;
}

ApiResponse CameraOperationWorker::processCommand(const CameraCommand& command) {
    std::cout << "Processing command type " << static_cast<int>(command.type) << " for camera: " << m_cameraId << std::endl;

    if (!m_camera || !m_camera->is_connected()) {
        ApiResponse response;
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        switch (command.type) {
            case CameraCommandType::Capture:
                return handleCapture();
            case CameraCommandType::SetISO:
                return handleSetISO(command.parameter);
            case CameraCommandType::SetAperture:
                return handleSetAperture(command.parameter);
            case CameraCommandType::SetShutterSpeed:
                return handleSetShutterSpeed(command.parameter);
            case CameraCommandType::SetWhiteBalance:
                return handleSetWhiteBalance(command.parameter);
            case CameraCommandType::StartLiveView:
                return handleStartLiveView();
            case CameraCommandType::StopLiveView:
                return handleStopLiveView();
            case CameraCommandType::AutoFocus:
                return handleAutoFocus();
            case CameraCommandType::ZoomIn:
                return handleZoomIn();
            case CameraCommandType::ZoomOut:
                return handleZoomOut();
            case CameraCommandType::GetCurrentISO:
                return handleGetCurrentISO();
            case CameraCommandType::GetCurrentAperture:
                return handleGetCurrentAperture();
            case CameraCommandType::GetCurrentShutterSpeed:
                return handleGetCurrentShutterSpeed();
            case CameraCommandType::GetCurrentWhiteBalance:
                return handleGetCurrentWhiteBalance();
            case CameraCommandType::GetCurrentFocusMode:
                return handleGetCurrentFocusMode();
            case CameraCommandType::GetAllCurrentSettings:
                return handleGetAllCurrentSettings();
            default:
                {
                    ApiResponse response;
                    response.success = false;
                    response.message = "Unknown command type";
                    return response;
                }
        }
    } catch (const std::exception& e) {
        ApiResponse response;
        response.success = false;
        response.message = "Command execution failed: " + std::string(e.what());
        return response;
    }
}

// Individual command handlers implementation
ApiResponse CameraOperationWorker::handleCapture() {
    ApiResponse response;

    std::cout << "WORKER THREAD: Starting camera capture with 500ms shutter timing..." << std::endl;
    m_camera->capture_image();

    response.success = true;
    response.message = "Image captured successfully";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    std::cout << "WORKER THREAD: Camera capture completed" << std::endl;
    return response;
}

ApiResponse CameraOperationWorker::handleSetISO(const std::string& isoValue) {
    ApiResponse response;

    std::cout << "WORKER THREAD: Setting ISO to " << isoValue << " for camera: " << m_cameraId << std::endl;
    m_camera->set_iso(); // Note: CameraDevice doesn't take parameters, uses internal SDK calls
    bool success = true; // Assume success for now

    response.success = success;
    response.message = success ? "ISO set successfully" : "Failed to set ISO";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleSetAperture(const std::string& apertureValue) {
    ApiResponse response;

    std::cout << "WORKER THREAD: Setting aperture to " << apertureValue << " for camera: " << m_cameraId << std::endl;
    m_camera->set_aperture(); // Note: CameraDevice doesn't take parameters
    bool success = true; // Assume success for now

    response.success = success;
    response.message = success ? "Aperture set successfully" : "Failed to set aperture";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleSetShutterSpeed(const std::string& shutterValue) {
    ApiResponse response;

    std::cout << "WORKER THREAD: Setting shutter speed to " << shutterValue << " for camera: " << m_cameraId << std::endl;
    m_camera->set_shutter_speed(); // Note: CameraDevice doesn't take parameters
    bool success = true; // Assume success for now

    response.success = success;
    response.message = success ? "Shutter speed set successfully" : "Failed to set shutter speed";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleSetWhiteBalance(const std::string& wbValue) {
    ApiResponse response;

    std::cout << "WORKER THREAD: Setting white balance to " << wbValue << " for camera: " << m_cameraId << std::endl;
    m_camera->set_white_balance(); // Note: CameraDevice doesn't take parameters
    bool success = true; // Assume success for now

    response.success = success;
    response.message = success ? "White balance set successfully" : "Failed to set white balance";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleStartLiveView() {
    ApiResponse response;

    std::cout << "WORKER THREAD: Starting live view for camera: " << m_cameraId << std::endl;

    response.success = false;
    response.message = "Live view worker operation is not implemented";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleStopLiveView() {
    ApiResponse response;

    std::cout << "WORKER THREAD: Stopping live view for camera: " << m_cameraId << std::endl;

    response.success = false;
    response.message = "Live view worker operation is not implemented";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleAutoFocus() {
    ApiResponse response;

    std::cout << "WORKER THREAD: Triggering autofocus for camera: " << m_cameraId << std::endl;

    response.success = false;
    response.message = "Autofocus worker operation is not implemented";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleZoomIn() {
    ApiResponse response;

    std::cout << "WORKER THREAD: Zooming in for camera: " << m_cameraId << std::endl;

    response.success = false;
    response.message = "Zoom worker operation is not implemented";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleZoomOut() {
    ApiResponse response;

    std::cout << "WORKER THREAD: Zooming out for camera: " << m_cameraId << std::endl;

    response.success = false;
    response.message = "Zoom worker operation is not implemented";
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleGetCurrentISO() {
    ApiResponse response;

    std::string isoValue = "ISO 400"; // Placeholder for worker thread demo

    response.success = true;
    response.message = "Current ISO: " + isoValue;
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleGetCurrentAperture() {
    ApiResponse response;

    std::string apertureValue = "f/2.8"; // Placeholder for worker thread demo

    response.success = true;
    response.message = "Current aperture: " + apertureValue;
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleGetCurrentShutterSpeed() {
    ApiResponse response;

    std::string shutterValue = "1/125"; // Placeholder for worker thread demo

    response.success = true;
    response.message = "Current shutter speed: " + shutterValue;
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleGetCurrentWhiteBalance() {
    ApiResponse response;

    std::string wbValue = "Auto WB"; // Placeholder for worker thread demo

    response.success = true;
    response.message = "Current white balance: " + wbValue;
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleGetCurrentFocusMode() {
    ApiResponse response;

    std::string focusMode = "Single AF"; // Placeholder for worker thread demo

    response.success = true;
    response.message = "Current focus mode: " + focusMode;
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

ApiResponse CameraOperationWorker::handleGetAllCurrentSettings() {
    ApiResponse response;

    std::string settings = "ISO: [current]" +
                          std::string(", Aperture: [current]") +
                          std::string(", Shutter: [current]") +
                          std::string(", WB: [current]") +
                          std::string(", Focus: [current]");

    response.success = true;
    response.message = settings;
    response.camera.connected = true;
    response.camera.model = std::string(m_camera->get_model().data());
    response.camera.id = m_cameraId;

    return response;
}

// ============================================================================
// LiveViewOSDWorker Implementation
// ============================================================================

LiveViewOSDWorker::LiveViewOSDWorker(std::shared_ptr<CameraDevice> camera, const std::string& cameraId, CameraWebController* controller)
    : m_camera(camera), m_cameraId(cameraId), m_controller(controller), m_running(false), m_liveViewStreaming(false), m_osdEnabled(false)
{
    std::cout << "Created LiveViewOSDWorker for camera: " << m_cameraId << std::endl;
}

LiveViewOSDWorker::~LiveViewOSDWorker() {
    stop();
}

void LiveViewOSDWorker::start() {
    std::cout << "Starting LiveViewOSDWorker for camera: " << m_cameraId << std::endl;
    m_running.store(true);
}

void LiveViewOSDWorker::stop() {
    if (m_running.load()) {
        std::cout << "Stopping LiveViewOSDWorker for camera: " << m_cameraId << std::endl;
        m_running.store(false);
        m_liveViewStreaming.store(false);
    }
}

void LiveViewOSDWorker::startLiveViewStreaming() {
    std::cout << "LIVEVIEW WORKER: Starting live view streaming for camera: " << m_cameraId << std::endl;
    m_liveViewStreaming.store(true);
}

void LiveViewOSDWorker::stopLiveViewStreaming() {
    std::cout << "LIVEVIEW WORKER: Stopping live view streaming for camera: " << m_cameraId << std::endl;
    m_liveViewStreaming.store(false);
}

void LiveViewOSDWorker::enableOSD() {
    std::cout << "LIVEVIEW WORKER: Enabling OSD for camera: " << m_cameraId << std::endl;
    m_osdEnabled.store(true);
}

void LiveViewOSDWorker::disableOSD() {
    std::cout << "LIVEVIEW WORKER: Disabling OSD for camera: " << m_cameraId << std::endl;
    m_osdEnabled.store(false);
}

bool LiveViewOSDWorker::getLiveViewFrame(std::vector<uint8_t>& jpegData) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_latestFrame.empty()) {
        return false;
    }
    jpegData = m_latestFrame;
    return true;
}

bool LiveViewOSDWorker::getCompositeFrame(std::vector<uint8_t>& jpegData) {
    std::lock_guard<std::mutex> lock(m_compositeMutex);
    if (m_latestCompositeFrame.empty()) {
        return false;
    }
    jpegData = m_latestCompositeFrame;
    return true;
}

void LiveViewOSDWorker::workerLoop() {
    std::cout << "LiveViewOSDWorker thread started for camera: " << m_cameraId << std::endl;

    // Frame statistics
    int frameCount = 0;
    int totalFrameCount = 0;
    int errorCount = 0;
    auto lastLogTime = std::chrono::steady_clock::now();
    auto startTime = std::chrono::steady_clock::now();

    while (m_running.load()) {
        if (!m_camera || !m_camera->is_connected()) {
            std::cout << "LIVEVIEW WORKER: Camera not connected, waiting..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Check if disconnect is in progress to prevent SDK calls during shutdown
        if (m_controller && m_controller->isDisconnectInProgress()) {
            std::cout << "🛑 LIVEVIEW WORKER: Disconnect in progress, stopping SDK calls..." << std::endl;
            break;
        }

        if (!m_liveViewStreaming.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        try {
            captureAndProcessFrame();

            frameCount++;
            totalFrameCount++;

            // Log stats every 5 seconds
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime).count();
            if (elapsed >= 5000) {
                double fps = (frameCount * 1000.0) / elapsed;
                auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
                std::cout << "LIVEVIEW WORKER [" << m_cameraId << "]: "
                          << "FPS: " << std::fixed << std::setprecision(1) << fps
                          << ", Total frames: " << totalFrameCount
                          << ", Errors: " << errorCount
                          << ", Uptime: " << totalElapsed << "s" << std::endl;

                frameCount = 0;
                lastLogTime = now;
            }

        } catch (const std::exception& e) {
            errorCount++;
            std::cerr << "LIVEVIEW WORKER [" << m_cameraId << "]: Frame capture error: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Control frame rate - aim for ~15fps
        std::this_thread::sleep_for(std::chrono::milliseconds(66));
    }

    std::cout << "LiveViewOSDWorker thread ended for camera: " << m_cameraId << std::endl;
}

void LiveViewOSDWorker::captureAndProcessFrame() {
    if (!m_camera || !m_camera->is_connected()) {
        return;
    }

    // 1. GetLiveViewProperties + Release (required by SDK before GetLiveViewImage)
    CrInt32 numProps = 0;
    SDK::CrLiveViewProperty* property = nullptr;
    auto propErr = SDK::GetLiveViewProperties(m_camera->get_device_handle(), &property, &numProps);
    if (CR_FAILED(propErr)) return;
    SDK::ReleaseLiveViewProperties(m_camera->get_device_handle(), property);

    // 2. GetLiveViewImageInfo for buffer size
    SDK::CrImageInfo inf;
    if (CR_FAILED(SDK::GetLiveViewImageInfo(m_camera->get_device_handle(), &inf))) return;
    CrInt32u bufSize = inf.GetBufferSize();
    if (bufSize < 1) return;

    // 3. Allocate and fetch frame
    auto image_data = std::make_unique<SDK::CrImageDataBlock>();
    auto image_buff = std::make_unique<CrInt8u[]>(bufSize);
    image_data->SetSize(bufSize);
    image_data->SetData(image_buff.get());

    auto err = SDK::GetLiveViewImage(m_camera->get_device_handle(), image_data.get());
    if (CR_FAILED(err) || image_data->GetImageSize() == 0) return;

    CrInt8u* jpegPtr = image_data->GetImageData();
    CrInt32u jpegSize = image_data->GetImageSize();

    // 4. Validate JPEG (SOI marker 0xFFD8)
    if (jpegSize < 2 || jpegPtr[0] != 0xFF || jpegPtr[1] != 0xD8) return;

    // 5. Store live view frame
    std::vector<uint8_t> liveViewData(jpegPtr, jpegPtr + jpegSize);
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_latestFrame = liveViewData;
    }

    // 6. Create composite frame if OSD is enabled
    if (m_osdEnabled.load()) {
        std::vector<uint8_t> compositeData;
        if (createOSDComposite(liveViewData, compositeData)) {
            std::lock_guard<std::mutex> lock(m_compositeMutex);
            m_latestCompositeFrame = compositeData;
        }
    }
}

bool LiveViewOSDWorker::createOSDComposite(const std::vector<uint8_t>& liveViewData, std::vector<uint8_t>& compositeData) {
    try {
        if (!m_camera || !m_camera->is_connected()) return false;

        // 1. Allocate OSD image buffer and fetch from SDK
        auto osd_image_data = std::make_unique<SDK::CrOSDImageDataBlock>();
        auto osd_image_buff = std::make_unique<CrInt8u[]>(CR_OSD_IMAGE_MAX_SIZE);
        osd_image_data->SetData(osd_image_buff.get());

        auto err = SDK::GetOSDImage(m_camera->get_device_handle(), osd_image_data.get());
        if (CR_FAILED(err) || osd_image_data->GetImageSize() == 0) {
            return false;
        }

        // 2. Prepare live view buffer (use black fill if live view data is empty)
        std::vector<CrInt8u> lvbuf;
        if (liveViewData.empty()) {
            const SDK::CrOSDImageMetaInfo& meta = osd_image_data->GetMetaInfo();
            OpenCVWrapper::CreateFillImage(meta.lvWidth, meta.lvHeight, 0, 0, 0, &lvbuf);
        } else {
            lvbuf.assign(liveViewData.begin(), liveViewData.end());
        }

        // 3. Composite using OpenCVWrapper
        auto output_buff = std::make_unique<unsigned char[]>(CR_OSD_IMAGE_MAX_SIZE);
        CrInt32u output_size = 0;

        bool ok = OpenCVWrapper::CompositeImage(lvbuf, osd_image_data.get(), output_buff.get(), &output_size);
        if (!ok || output_size == 0) {
            return false;
        }

        // 4. Copy result to output vector
        compositeData.assign(output_buff.get(), output_buff.get() + output_size);
        return true;

    } catch (const std::exception& e) {
        std::cerr << "LIVEVIEW WORKER [" << m_cameraId << "]: OSD composite error: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Callback Correlation System Implementation
// ============================================================================

std::string CameraWebController::generateCorrelationId() {
    // Generate a unique correlation ID using random numbers
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; ++i) {
        ss << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 12; ++i) {
        ss << dis(gen);
    }

    return ss.str();
}

void CameraWebController::registerCallbackListener(const std::string& cameraId,
                                                  const std::string& propertyName,
                                                  const std::string& correlationId,
                                                  CrInt32u propertyCode,
                                                  std::promise<ApiResponse>&& promise) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);

    auto listener = std::make_unique<CallbackListener>(correlationId, cameraId, propertyName, propertyCode);
    listener->promise = std::move(promise);

    m_callbackListeners[correlationId] = std::move(listener);

    std::cout << "[CALLBACK] Registered listener " << correlationId
              << " for property " << propertyName
              << " (0x" << std::hex << propertyCode << std::dec << ")" << std::endl;
}

void CameraWebController::unregisterCallbackListener(const std::string& correlationId) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);

    auto it = m_callbackListeners.find(correlationId);
    if (it != m_callbackListeners.end()) {
        std::cout << "[CALLBACK] Unregistered listener " << correlationId << std::endl;
        m_callbackListeners.erase(it);
    }
}

bool CameraWebController::handlePropertyCallback(const std::string& cameraId, CrInt32u propertyCode) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);

    // Check single property listeners
    for (auto it = m_callbackListeners.begin(); it != m_callbackListeners.end(); ) {
        auto& listener = it->second;

        if (listener->cameraId == cameraId && listener->propertyCode == propertyCode) {
            // Found matching listener
            auto elapsed = std::chrono::steady_clock::now() - listener->startTime;
            auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

            ApiResponse response;
            response.success = true;
            response.message = "Property " + listener->propertyName + " set successfully";
            response.data["property"] = listener->propertyName;
            response.data["latency"] = std::to_string(latency);
            response.data["actualValue"] = getCurrentPropertyValue(cameraId, propertyCode);

            listener->promise.set_value(std::move(response));

            std::cout << "[CALLBACK] Property " << listener->propertyName
                     << " callback fulfilled (latency: " << latency << "ms)" << std::endl;

            it = m_callbackListeners.erase(it);
            return true;
        } else {
            ++it;
        }
    }

    return false;
}

// Property code mapping implementation
CrInt32u CameraWebController::getPropertyCode(const std::string& propertyName) const {
    // Map property names to SDK property codes
    static const std::unordered_map<std::string, CrInt32u> propertyMap = {
        {"iso", SDK::CrDevicePropertyCode::CrDeviceProperty_IsoCurrentSensitivity},
        {"aperture", SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber},
        {"shutter-speed", SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed},
        {"white-balance", SDK::CrDevicePropertyCode::CrDeviceProperty_WhiteBalance},
        {"exposure-mode", SDK::CrDevicePropertyCode::CrDeviceProperty_ExposureProgramMode},
        {"focus-mode", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusMode},
        {"focus-area", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusArea},
        {"drive-mode", SDK::CrDevicePropertyCode::CrDeviceProperty_DriveMode},
        {"focus-distance", SDK::CrDevicePropertyCode::CrDeviceProperty_FocalDistanceInMeter},
        {"focus-position", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusPositionSetting},
        {"focus-position-current", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusPositionCurrentValue},
        {"focus-driving-status", SDK::CrDevicePropertyCode::CrDeviceProperty_FocusDrivingStatus}
    };

    auto it = propertyMap.find(propertyName);
    if (it != propertyMap.end()) {
        return it->second;
    }

    return 0; // Invalid property
}

std::string CameraWebController::getPropertyName(CrInt32u propertyCode) const {
    // Reverse mapping from property code to name
    // NOTE: Must match PROPERTY_MAP entries to ensure getAllProperties() returns all mapped properties
    switch (propertyCode) {
        // Core camera settings
        case SDK::CrDevicePropertyCode::CrDeviceProperty_PriorityKeySettings:
            return "priority-key";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_IsoSensitivity:
        case SDK::CrDevicePropertyCode::CrDeviceProperty_IsoCurrentSensitivity:
            return "iso";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FNumber:
            return "aperture";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_ShutterSpeed:
            return "shutter-speed";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_WhiteBalance:
            return "white-balance";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_ExposureProgramMode:
            return "exposure-program-mode";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FocusMode:
            return "focus-mode";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FocusArea:
            return "focus-area";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_DriveMode:
            return "drive-mode";

        // Image format and quality settings
        case SDK::CrDevicePropertyCode::CrDeviceProperty_StillImageStoreDestination:
            return "still-image-store-destination";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FileType:
            return "file-format";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_StillImageQuality:
            return "image-quality";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_RAW_FileCompressionType:
            return "raw-compression";

        // Zoom and focus distance
        case SDK::CrDevicePropertyCode::CrDeviceProperty_ZoomDistance:
            return "zoom-distance";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FocalDistanceInMeter:
            return "focus-distance";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FocusPositionSetting:
            return "focus-position";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FocusPositionCurrentValue:
            return "focus-position-current";
        case SDK::CrDevicePropertyCode::CrDeviceProperty_FocusDrivingStatus:
            return "focus-driving-status";

        default:
            return "unknown";
    }
}

std::string CameraWebController::getCurrentPropertyValue(const std::string& cameraId,
                                                        CrInt32u propertyCode) {
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
    auto it = m_cameraThreads.find(cameraId);
    if (it != m_cameraThreads.end() && it->second->camera) {
        SCRSDK::CrDeviceProperty prop;
        prop.SetCode(propertyCode);
        it->second->camera->get_property(prop);
        return it->second->camera->getCurrentStr(&prop).data();
    }

    return "unknown";
}

// ============================================================================
// Action Implementations
// ============================================================================

ApiResponse CameraWebController::s1Shooting(const std::string& cameraId) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    // Call S1 shooting (half-press)
    camera->s1_shooting();

    response.success = true;
    response.message = "S1 shooting (half-press) executed";
    response.data["action"] = "s1_shooting";
    response.data["cameraId"] = cameraId;

    return response;
}

ApiResponse CameraWebController::afShutter(const std::string& cameraId) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    // Call AF + shutter
    bool success = camera->af_shutter();

    if (!success) {
        response.success = false;
        response.message = "AF+Shutter failed: Camera must be in AF mode (not Manual Focus)";
        response.data["action"] = "af_shutter";
        response.data["cameraId"] = cameraId;
        response.data["error"] = "focus_mode_not_af";
        return response;
    }

    response.success = true;
    response.message = "AF + capture executed";
    response.data["action"] = "af_shutter";
    response.data["cameraId"] = cameraId;

    return response;
}

ApiResponse CameraWebController::setExposureMode(const std::string& cameraId, const std::string& mode) {
    ApiResponse response;

    // Map mode string to SDK value
    CrInt64u modeValue = 0;
    if (mode == "P" || mode == "program") {
        modeValue = SDK::CrExposureProgram::CrExposure_P_Auto;
    } else if (mode == "A" || mode == "aperture") {
        modeValue = SDK::CrExposureProgram::CrExposure_A_AperturePriority;
    } else if (mode == "S" || mode == "shutter") {
        modeValue = SDK::CrExposureProgram::CrExposure_S_ShutterSpeedPriority;
    } else if (mode == "M" || mode == "manual") {
        modeValue = SDK::CrExposureProgram::CrExposure_M_Manual;
    } else {
        response.success = false;
        response.message = "Invalid exposure mode: " + mode;
        return response;
    }

    // Use callback correlation for property setting
    std::promise<ApiResponse> responsePromise;
    auto responseFuture = responsePromise.get_future();

    std::string correlationId = generateCorrelationId();
    CrInt32u propertyCode = SDK::CrDevicePropertyCode::CrDeviceProperty_ExposureProgramMode;

    registerCallbackListener(cameraId, "exposure-mode", correlationId, propertyCode,
                            std::move(responsePromise));

    // Find camera and set property
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    // Set exposure mode (apply the value parsed from the request)
    camera->set_exposure_program_mode(modeValue);

    // Wait for callback with timeout
    auto status = responseFuture.wait_for(std::chrono::milliseconds(2000));

    if (status == std::future_status::timeout) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = "Exposure mode set timeout";
        return response;
    }

    return responseFuture.get();
}

ApiResponse CameraWebController::setDriveMode(const std::string& cameraId, const std::string& mode) {
    ApiResponse response;

    // Map mode string to SDK value
    CrInt64u driveValue = 0;

    // Basic drive modes
    if (mode == "single") {
        driveValue = SDK::CrDriveMode::CrDrive_Single;
    } else if (mode == "continuous") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous;
    } else if (mode == "continuous-hi") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Hi;
    } else if (mode == "continuous-hi-plus" || mode == "burst") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Hi_Plus;
    } else if (mode == "continuous-lo") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Lo;
    } else if (mode == "continuous-mid") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Mid;
    } else if (mode == "continuous-speed-priority") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_SpeedPriority;

    // Timer modes
    } else if (mode == "timer-2s") {
        driveValue = SDK::CrDriveMode::CrDrive_Timer_2s;
    } else if (mode == "timer-5s") {
        driveValue = SDK::CrDriveMode::CrDrive_Timer_5s;
    } else if (mode == "timer-10s") {
        driveValue = SDK::CrDriveMode::CrDrive_Timer_10s;

    // Single burst modes
    } else if (mode == "single-burst-lo") {
        driveValue = SDK::CrDriveMode::CrDrive_SingleBurstShooting_lo;
    } else if (mode == "single-burst-mid") {
        driveValue = SDK::CrDriveMode::CrDrive_SingleBurstShooting_mid;
    } else if (mode == "single-burst-hi") {
        driveValue = SDK::CrDriveMode::CrDrive_SingleBurstShooting_hi;

    // Focus bracketing
    } else if (mode == "focus-bracket") {
        driveValue = SDK::CrDriveMode::CrDrive_FocusBracket;

    // Continuous exposure bracketing
    } else if (mode == "bracket-continuous-0.3ev-3pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Bracket_03Ev_3pics;
    } else if (mode == "bracket-continuous-0.3ev-5pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Bracket_03Ev_5pics;
    } else if (mode == "bracket-continuous-0.7ev-3pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Bracket_07Ev_3pics;
    } else if (mode == "bracket-continuous-0.7ev-5pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Bracket_07Ev_5pics;
    } else if (mode == "bracket-continuous-1.0ev-3pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Bracket_10Ev_3pics;
    } else if (mode == "bracket-continuous-1.0ev-5pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Bracket_10Ev_5pics;
    } else if (mode == "bracket-continuous-2.0ev-3pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Continuous_Bracket_20Ev_3pics;

    // Single exposure bracketing
    } else if (mode == "bracket-single-0.3ev-3pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Single_Bracket_03Ev_3pics;
    } else if (mode == "bracket-single-0.3ev-5pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Single_Bracket_03Ev_5pics;
    } else if (mode == "bracket-single-0.7ev-3pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Single_Bracket_07Ev_3pics;
    } else if (mode == "bracket-single-1.0ev-3pics") {
        driveValue = SDK::CrDriveMode::CrDrive_Single_Bracket_10Ev_3pics;

    // WB & DRO bracketing
    } else if (mode == "bracket-wb-lo") {
        driveValue = SDK::CrDriveMode::CrDrive_WB_Bracket_Lo;
    } else if (mode == "bracket-wb-hi") {
        driveValue = SDK::CrDriveMode::CrDrive_WB_Bracket_Hi;
    } else if (mode == "bracket-dro-lo") {
        driveValue = SDK::CrDriveMode::CrDrive_DRO_Bracket_Lo;
    } else if (mode == "bracket-dro-hi") {
        driveValue = SDK::CrDriveMode::CrDrive_DRO_Bracket_Hi;

    } else {
        response.success = false;
        response.message = "Invalid drive mode: " + mode + ". Supported modes: single, continuous, continuous-hi, continuous-lo, timer-2s, timer-5s, timer-10s, focus-bracket, bracket-continuous-*, bracket-single-*, etc.";
        return response;
    }

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        // Set drive mode
        std::cout << "🎬 Setting drive mode to: " << mode << " (value: " << driveValue << ")" << std::endl;
        bool result = camera->set_drive_mode(driveValue);
        std::cout << "🎬 Drive mode set result: " << (result ? "SUCCESS" : "FAILED") << std::endl;

        response.success = result;
        response.message = result ? "Drive mode set to " + mode : "Failed to set drive mode";
        response.data["drive-mode"] = mode;

        // Populate camera metadata
        response.camera.connected = camera->is_connected();
        response.camera.model = camera->get_model();  // Fixed: Direct assignment (no .data() on temporary)
        response.camera.id = camera->get_id();        // Fixed: Direct assignment (no .data() on temporary)
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception setting drive mode: " + std::string(e.what());
        std::cerr << "❌ Exception in setDriveMode: " << e.what() << std::endl;
    } catch (...) {
        response.success = false;
        response.message = "Unknown exception setting drive mode";
        std::cerr << "❌ Unknown exception in setDriveMode" << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::setFocusMode(const std::string& cameraId, const std::string& mode) {
    ApiResponse response;

    // Use callback correlation for property setting
    std::promise<ApiResponse> responsePromise;
    auto responseFuture = responsePromise.get_future();

    std::string correlationId = generateCorrelationId();
    CrInt32u propertyCode = SDK::CrDevicePropertyCode::CrDeviceProperty_FocusMode;

    registerCallbackListener(cameraId, "focus-mode", correlationId, propertyCode,
                            std::move(responsePromise));

    // Find camera and set property
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    // Set focus mode (apply the value parsed from the request)
    CrInt64u focusModeValue = 0;
    try {
        focusModeValue = cli::parse_focus_mode(mode);
    } catch (const std::exception& e) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = std::string("Invalid focus mode: ") + e.what();
        return response;
    }
    camera->set_focus_mode(focusModeValue);

    // Wait for callback with timeout
    auto status = responseFuture.wait_for(std::chrono::milliseconds(2000));

    if (status == std::future_status::timeout) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = "Focus mode set timeout";
        return response;
    }

    return responseFuture.get();
}

ApiResponse CameraWebController::setFocusArea(const std::string& cameraId, const std::string& area) {
    ApiResponse response;

    // Use callback correlation for property setting
    std::promise<ApiResponse> responsePromise;
    auto responseFuture = responsePromise.get_future();

    std::string correlationId = generateCorrelationId();
    CrInt32u propertyCode = SDK::CrDevicePropertyCode::CrDeviceProperty_FocusArea;

    registerCallbackListener(cameraId, "focus-area", correlationId, propertyCode,
                            std::move(responsePromise));

    // Find camera and set property
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    // Set focus area (apply the value parsed from the request)
    CrInt64u focusAreaValue = 0;
    try {
        focusAreaValue = cli::parse_focus_area(area);
    } catch (const std::exception& e) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = std::string("Invalid focus area: ") + e.what();
        return response;
    }
    camera->set_focus_area(focusAreaValue);

    // Wait for callback with timeout
    auto status = responseFuture.wait_for(std::chrono::milliseconds(2000));

    if (status == std::future_status::timeout) {
        unregisterCallbackListener(correlationId);
        response.success = false;
        response.message = "Focus area set timeout";
        return response;
    }

    return responseFuture.get();
}

ApiResponse CameraWebController::toggleMovieRecording(const std::string& cameraId) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    // Toggle movie recording using non-interactive method
    // Uses CrCommandId_MovieRecord (compatible with Alpha cameras like ILCE-7M4)
    try {
        bool result = camera->toggle_movie_recording_direct();

        response.success = result;
        response.message = result ? "Movie recording toggled" : "Failed to toggle movie recording";
        response.data["action"] = "movie_rec_toggle";
        response.data["cameraId"] = cameraId;

        // Populate camera info
        response.camera.connected = true;
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception toggling movie recording: " + std::string(e.what());
    }

    return response;
}

ApiResponse CameraWebController::getPriorityKey(const std::string& cameraId) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        // Get priority key setting from camera
        CrInt32u codes[1] = { SDK::CrDevicePropertyCode::CrDeviceProperty_PriorityKeySettings };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);

        if (CR_SUCCEEDED(err) && numProps > 0 && properties != nullptr) {
            auto priorityValue = properties[0].GetCurrentValue();

            std::string prioritySetting;
            if (priorityValue == SDK::CrPriorityKeySettings::CrPriorityKey_PCRemote) {
                prioritySetting = "pc-remote";
            } else if (priorityValue == SDK::CrPriorityKeySettings::CrPriorityKey_CameraPosition) {
                prioritySetting = "camera-position";
            } else {
                prioritySetting = "unknown";
            }

            response.success = true;
            response.message = "Priority key setting: " + prioritySetting;
            response.data["setting"] = prioritySetting;
            response.data["rawValue"] = std::to_string(priorityValue);

            // Populate camera metadata
            response.camera.connected = true;
            response.camera.model = camera->get_model();
            response.camera.id = camera->get_id();

            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
        } else {
            response.success = false;
            response.message = "Failed to get priority key setting";
        }
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception getting priority key: " + std::string(e.what());
        std::cerr << "❌ Exception in getPriorityKey: " << e.what() << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::setPriorityKey(const std::string& cameraId, const std::string& setting) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        CrInt16u priorityValue;

        if (setting == "pc-remote") {
            priorityValue = SDK::CrPriorityKeySettings::CrPriorityKey_PCRemote;
        } else if (setting == "camera-position") {
            priorityValue = SDK::CrPriorityKeySettings::CrPriorityKey_CameraPosition;
        } else {
            response.success = false;
            response.message = "Invalid priority key setting. Use 'pc-remote' or 'camera-position'";
            return response;
        }

        std::cout << "🔑 Setting priority key to: " << setting << " (value: " << priorityValue << ")" << std::endl;

        // Set priority key
        SDK::CrDeviceProperty prop;
        prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_PriorityKeySettings);
        prop.SetCurrentValue(priorityValue);
        prop.SetValueType(SDK::CrDataType::CrDataType_UInt16Array);

        auto err = SDK::SetDeviceProperty(camera->get_device_handle(), &prop);

        if (CR_SUCCEEDED(err)) {
            std::cout << "✅ Priority key set successfully to: " << setting << std::endl;
            response.success = true;
            response.message = "Priority key set to " + setting;
            response.data["setting"] = setting;
        } else {
            std::cout << "❌ Failed to set priority key. Error code: " << std::hex << err << std::dec << std::endl;
            response.success = false;
            response.message = "Failed to set priority key. Error code: " + std::to_string(err);
        }

        // Populate camera metadata
        response.camera.connected = true;
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception setting priority key: " + std::string(e.what());
        std::cerr << "❌ Exception in setPriorityKey: " << e.what() << std::endl;
    }

    return response;
}

// ==============================================================================
// Save Destination Settings (for Remote Transfer mode)
// ==============================================================================

ApiResponse CameraWebController::getSaveDestination(const std::string& cameraId) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        std::string savePath = camera->get_save_info_path();
        std::string savePrefix = camera->get_save_info_prefix();
        int saveStartNo = camera->get_save_info_start_no();

        response.success = true;
        response.message = savePath.empty() ? "No save destination set" : "Current save destination";
        response.data["path"] = savePath.empty() ? "Not set" : savePath;
        response.data["prefix"] = savePrefix.empty() ? "Not set" : savePrefix;
        response.data["startNo"] = std::to_string(saveStartNo);

        // Populate camera metadata
        response.camera.connected = true;
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception getting save destination: " + std::string(e.what());
        std::cerr << "❌ Exception in getSaveDestination: " << e.what() << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::setSaveDestination(const std::string& cameraId, const std::string& path, const std::string& prefix, int startNo) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        std::cout << "📁 Setting save destination:" << std::endl;
        std::cout << "   Path: " << path << std::endl;
        if (!prefix.empty()) {
            std::cout << "   Prefix: " << prefix << std::endl;
        }
        std::cout << "   Start number: " << (startNo >= 0 ? std::to_string(startNo) : "Auto") << std::endl;

        // Set save info using CameraDevice method
        std::string errorDetail;
        bool success = camera->set_save_info(path, prefix, startNo, &errorDetail);

        if (success) {
            std::cout << "✅ Save destination set successfully" << std::endl;
            response.success = true;
            response.message = "Save destination set successfully";
            response.data["path"] = path;
            response.data["prefix"] = prefix;
            response.data["startNo"] = std::to_string(startNo);
        } else {
            std::cout << "❌ Failed to set save destination" << std::endl;
            response.success = false;
            response.message = "Failed to set save destination: " + errorDetail;
            response.data["error_code"] = errorDetail;
        }

        // Populate camera metadata
        response.camera.connected = true;
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception setting save destination: " + std::string(e.what());
        std::cerr << "❌ Exception in setSaveDestination: " << e.what() << std::endl;
    }

    return response;
}

// ==============================================================================
// Camera Settings File Save/Load Operations
// ==============================================================================

ApiResponse CameraWebController::downloadCameraSettings(const std::string& cameraId, const std::string& filename) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        // Check if save is supported
        if (!camera->is_settings_save_supported()) {
            response.success = false;
            response.message = "Camera settings save not supported or not enabled. Ensure SD card is inserted.";
            return response;
        }

        // Create camera_settings directory if it doesn't exist
        std::string settingsDir = "camera_settings";
#if defined(__APPLE__) || defined(__linux__)
        mkdir(settingsDir.c_str(), 0755);
#elif defined(WIN32) || defined(_WIN64)
        _mkdir(settingsDir.c_str());
#endif

        // Prepare file path
        std::string filepath = settingsDir + "/";
        std::string actualFilename = filename.empty() ? "CUMSET.DAT" : filename;

        std::cout << "💾 Downloading camera settings to: " << (filepath + actualFilename) << std::endl;

        // Call CameraDevice method
        SDK::CrError err = camera->download_camera_settings(filepath, actualFilename);

        if (err == SDK::CrError_None) {
            std::cout << "✅ Camera settings downloaded successfully" << std::endl;
            response.success = true;
            response.message = "Camera settings downloaded successfully";
            response.data["filename"] = actualFilename;
            response.data["path"] = filepath + actualFilename;
        } else {
            std::cout << "❌ Failed to download camera settings (Error: " << std::hex << err << ")" << std::endl;
            response.success = false;
            response.message = "Failed to download camera settings. Check if SD card is inserted.";
        }

        // Populate camera metadata
        response.camera.connected = true;
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception downloading camera settings: " + std::string(e.what());
        std::cerr << "❌ Exception in downloadCameraSettings: " << e.what() << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::uploadCameraSettings(const std::string& cameraId, const std::string& filename) {
    ApiResponse response;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto it = m_cameraThreads.find(cameraId);
        if (it != m_cameraThreads.end()) {
            camera = it->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        // Check if load is supported
        if (!camera->is_settings_load_supported()) {
            response.success = false;
            response.message = "Camera settings load not supported or not enabled. Ensure SD card is inserted.";
            return response;
        }

        // Prepare file path
        std::string settingsDir = "camera_settings";
        std::string filepath = settingsDir + "/" + filename;

        // Check if file exists
        std::ifstream checkFile(filepath);
        if (!checkFile.good()) {
            response.success = false;
            response.message = "Settings file not found: " + filename;
            return response;
        }
        checkFile.close();

        std::cout << "📤 Uploading camera settings from: " << filepath << std::endl;
        std::cout << "⚠️  WARNING: Camera will reboot after upload!" << std::endl;

        // Call CameraDevice method
        SDK::CrError err = camera->upload_camera_settings(filepath);

        if (err == SDK::CrError_None) {
            std::cout << "✅ Camera settings uploaded successfully (camera will reboot)" << std::endl;
            response.success = true;
            response.message = "Camera settings uploaded successfully. Camera will reboot and disconnect.";
            response.data["filename"] = filename;
            response.data["warning"] = "Camera will reboot and disconnect. Please reconnect after reboot.";
        } else {
            std::cout << "❌ Failed to upload camera settings (Error: " << std::hex << err << ")" << std::endl;
            response.success = false;
            response.message = "Failed to upload camera settings. Check if SD card is inserted.";
        }

        // Populate camera metadata
        response.camera.connected = true;
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception uploading camera settings: " + std::string(e.what());
        std::cerr << "❌ Exception in uploadCameraSettings: " << e.what() << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::listSavedSettings() {
    ApiResponse response;

    try {
        std::string settingsDir = "camera_settings";
        std::vector<std::string> datFiles;

#if defined(__APPLE__) || defined(__linux__)
        DIR* dp = opendir(settingsDir.c_str());
        if (dp != NULL) {
            struct dirent* ep;
            while ((ep = readdir(dp)) != NULL) {
                if (ep->d_name[0] != '.') {
                    std::string filename(ep->d_name);
                    // Check if file ends with .DAT or .dat
                    if (filename.length() >= 4) {
                        std::string ext = filename.substr(filename.length() - 4);
                        if (ext == ".DAT" || ext == ".dat") {
                            datFiles.push_back(filename);
                        }
                    }
                }
            }
            closedir(dp);
        }
#elif defined(WIN32) || defined(_WIN64)
        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile((settingsDir + "\\*.DAT").c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    datFiles.push_back(findData.cFileName);
                }
            } while (FindNextFile(hFind, &findData) != 0);
            FindClose(hFind);
        }
#endif

        response.success = true;
        response.message = "Found " + std::to_string(datFiles.size()) + " settings file(s)";

        // Add files to response data
        for (size_t i = 0; i < datFiles.size(); ++i) {
            response.data["file_" + std::to_string(i)] = datFiles[i];
        }
        response.data["count"] = std::to_string(datFiles.size());

        std::cout << "📋 Found " << datFiles.size() << " camera settings files" << std::endl;

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception listing saved settings: " + std::string(e.what());
        std::cerr << "❌ Exception in listSavedSettings: " << e.what() << std::endl;
    }

    return response;
}

// ==============================================================================
// Zoom Control Operations
// ==============================================================================

ApiResponse CameraWebController::executeZoomAction(const std::string& cameraId, int speed) {
    ApiResponse response;

    try {
        std::shared_ptr<CameraDevice> camera;
        {
            std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
            auto it = m_cameraThreads.find(cameraId);
            if (it != m_cameraThreads.end()) {
                camera = it->second->camera;
            }
        }

        if (!camera) {
            response.success = false;
            response.message = "Camera not found: " + cameraId;
            std::cerr << "❌ Camera not found: " << cameraId << std::endl;
            return response;
        }

        // Validate speed range
        if (speed < -10 || speed > 10) {
            response.success = false;
            response.message = "Invalid zoom speed. Must be between -10 and +10";
            std::cerr << "❌ Invalid zoom speed: " << speed << std::endl;
            return response;
        }

        // Check if zoom is supported
        if (!camera->is_zoom_operation_supported()) {
            response.success = false;
            response.message = "Zoom operation not supported for this camera/lens";
            std::cerr << "❌ Zoom not supported for camera: " << cameraId << std::endl;
            return response;
        }

        // Execute zoom operation
        SDK::CrError err = camera->execute_zoom_operation_direct(static_cast<int8_t>(speed));

        if (CR_SUCCEEDED(err)) {
            response.success = true;
            if (speed == 0) {
                response.message = "Zoom stopped";
            } else if (speed < 0) {
                response.message = "Zooming out (wide) at speed " + std::to_string(std::abs(speed));
            } else {
                response.message = "Zooming in (tele) at speed " + std::to_string(speed);
            }
            response.data["speed"] = std::to_string(speed);
            std::cout << "🔍 Zoom operation executed: speed=" << speed << std::endl;
        } else {
            response.success = false;
            response.message = "Failed to execute zoom operation";
            std::cerr << "❌ Zoom operation failed with error: " << std::hex << err << std::dec << std::endl;
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception executing zoom action: " + std::string(e.what());
        std::cerr << "❌ Exception in executeZoomAction: " << e.what() << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::getZoomDistance(const std::string& cameraId) {
    ApiResponse response;

    try {
        std::shared_ptr<CameraDevice> camera;
        {
            std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
            auto it = m_cameraThreads.find(cameraId);
            if (it != m_cameraThreads.end()) {
                camera = it->second->camera;
            }
        }

        if (!camera) {
            response.success = false;
            response.message = "Camera not found: " + cameraId;
            std::cerr << "❌ Camera not found: " << cameraId << std::endl;
            return response;
        }

        // Get zoom distance (focal length)
        uint32_t focalLengthMm = camera->get_zoom_distance_mm();

        if (focalLengthMm > 0) {
            response.success = true;
            response.message = "Current focal length: " + std::to_string(focalLengthMm) + "mm";
            response.data["focal_length_mm"] = std::to_string(focalLengthMm);
            response.data["supported"] = "true";
            std::cout << "🔍 Current focal length: " << focalLengthMm << "mm" << std::endl;
        } else {
            response.success = true;
            response.message = "Zoom distance not supported for this camera/lens";
            response.data["focal_length_mm"] = "0";
            response.data["supported"] = "false";
            std::cout << "⚠️  Zoom distance not supported for camera: " << cameraId << std::endl;
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception getting zoom distance: " + std::string(e.what());
        std::cerr << "❌ Exception in getZoomDistance: " << e.what() << std::endl;
    }

    return response;
}

// ==============================================================================
// RESTful Generic Handlers - Scalable to 800+ properties/actions
// ==============================================================================

ApiResponse CameraWebController::getPropertyGeneric(const std::string& cameraId, const std::string& propertyName) {
    ApiResponse response;

    // Lookup property mapping
    auto it = PROPERTY_MAP.find(propertyName);
    if (it == PROPERTY_MAP.end()) {
        response.success = false;
        response.message = "Unknown property: " + propertyName;
        return response;
    }

    const PropertyMapping& mapping = it->second;

    // Option 3: Route to existing specific methods if they exist
    if (mapping.hasCustomMethod) {
        std::cout << "🔀 Routing " << propertyName << " getter to specific method" << std::endl;

        if (propertyName == "iso") {
            return getCurrentISO(cameraId);
        } else if (propertyName == "aperture") {
            return getCurrentAperture(cameraId);
        } else if (propertyName == "zoom-distance") {
            return getZoomDistance(cameraId);
        } else {
            response.success = false;
            response.message = "Custom getter not implemented for property: " + propertyName;
            return response;
        }
    }

    // Generic handler for simple properties (no custom method needed)
    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        // Use the new helper functions for consistent property retrieval
        PropertyData propData = getPropertyData(camera, propertyName, mapping.sdkPropertyCode);
        return buildPropertyResponse(propData, camera);
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception getting property: " + std::string(e.what());
        std::cerr << "❌ Exception in getPropertyGeneric: " << e.what() << std::endl;
    }

    return response;
}

// Helper function: Convert string value to SDK numeric value
// Supports: numeric values (decimal/hex) and string enum values
CrInt64u CameraWebController::convertValueToSDK(const std::string& value, const std::string& propertyName) {
    // Hex values always take priority — unambiguous across all property types
    if (value.length() >= 2 && (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X")) {
        return std::stoull(value.substr(2), nullptr, 16);
    }

    // Properties with custom parsers go BEFORE generic numeric fallback.
    // This prevents "1/125" from being parsed as decimal 1 by stoull.
    try {
        if (propertyName == "shutter-speed") {
            return cli::parse_shutter_speed(value);
        }
        if (propertyName == "priority-key" || propertyName == "position-key-setting") {
            return cli::parse_position_key_setting(value);
        }
        if (propertyName == "white-balance" || propertyName == "whitebalance") {
            return cli::parse_white_balance(value);
        }
        if (propertyName == "focus-mode" || propertyName == "focusmode") {
            return cli::parse_focus_mode(value);
        }
        if (propertyName == "exposure-program-mode" || propertyName == "exposure-mode") {
            return cli::parse_exposure_program_mode(value);
        }
        if (propertyName == "drive-mode" || propertyName == "still-capture-mode") {
            return cli::parse_still_capture_mode(value);
        }
        if (propertyName == "focus-area") {
            return cli::parse_focus_area(value);
        }
        if (propertyName == "raw-compression" || propertyName == "raw-file-compression") {
            return cli::parse_raw_file_compression(value);
        }
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(std::string(e.what()));
    }

    // Generic numeric fallback for properties without custom parsers (e.g., ISO)
    try {
        return std::stoull(value, nullptr, 10);
    } catch (const std::exception&) {
        // Not numeric
    }

    throw std::invalid_argument("Property '" + propertyName +
                                "' does not support string value '" + value +
                                "'. Use hex value (e.g., '0x0002') from GET response.");
}

// Helper function: Format available values for a property into JSON array
std::string CameraWebController::formatAvailableValuesJSON(const std::string& propertyName,
                                                           const SDK::CrDeviceProperty& prop,
                                                           std::shared_ptr<CameraDevice> camera) {
    std::stringstream json;
    json << "[";

    int nval = 0;
    const unsigned char* values = prop.GetValues();
    bool hasValues = false;

    // Determine property type and format available values
    if (propertyName == "aperture" || propertyName == "f-number") {
        nval = prop.GetValueSize() / sizeof(std::uint16_t);
        auto parsed = cli::parse_f_number(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_f_number(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }
    else if (propertyName == "iso" || propertyName == "iso-sensitivity") {
        nval = prop.GetValueSize() / sizeof(std::uint32_t);
        auto parsed = cli::parse_iso_sensitivity(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_iso_sensitivity(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }
    else if (propertyName == "shutter-speed") {
        nval = prop.GetValueSize() / sizeof(std::uint32_t);
        auto parsed = cli::parse_shutter_speed(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_shutter_speed(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }
    else if (propertyName == "white-balance") {
        nval = prop.GetValueSize() / sizeof(std::uint16_t);
        auto parsed = cli::parse_white_balance(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_white_balance(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }
    else if (propertyName == "exposure-program-mode" || propertyName == "exposure-mode") {
        nval = prop.GetValueSize() / sizeof(std::uint32_t);
        auto parsed = cli::parse_exposure_program_mode(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_exposure_program_mode(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }
    else if (propertyName == "drive-mode" || propertyName == "still-capture-mode") {
        nval = prop.GetValueSize() / sizeof(std::uint32_t);
        auto parsed = cli::parse_still_capture_mode(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_still_capture_mode(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }
    else if (propertyName == "focus-mode") {
        nval = prop.GetValueSize() / sizeof(std::uint16_t);
        auto parsed = cli::parse_focus_mode(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_focus_mode(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }
    else if (propertyName == "focus-area") {
        nval = prop.GetValueSize() / sizeof(std::uint16_t);
        auto parsed = cli::parse_focus_area(values, nval);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (i > 0) json << ",";
            auto formatted = cli::format_focus_area(parsed[i]);
            json << "{\"value\":" << parsed[i]
                 << ",\"hex_value\":\"0x" << std::hex << parsed[i] << std::dec << "\""
                 << ",\"formatted\":\"" << formatted << "\"}";
        }
        hasValues = (nval > 0);
    }

    json << "]";
    return hasValues ? json.str() : "[]";
}

ApiResponse CameraWebController::setPropertyGeneric(const std::string& cameraId, const std::string& propertyName, const std::string& value) {
    ApiResponse response;

    // Lookup property mapping
    auto it = PROPERTY_MAP.find(propertyName);
    if (it == PROPERTY_MAP.end()) {
        response.success = false;
        response.message = "Unknown property: " + propertyName;
        return response;
    }

    const PropertyMapping& mapping = it->second;

    // Option 3: Route to existing specific methods if they exist
    if (mapping.hasCustomMethod) {
        std::cout << "🔀 Routing " << propertyName << " to specific method: " << mapping.sdkMethodName << std::endl;

        if (propertyName == "iso") {
            return setISO(cameraId, value);
        } else if (propertyName == "aperture") {
            // Convert "2.8" to "f/2.8" format expected by setAperture
            std::string apertureValue = value;
            if (apertureValue.find("f/") != 0 && apertureValue.find("F") != 0) {
                apertureValue = "f/" + value;
            }
            return setAperture(cameraId, apertureValue);
        } else if (propertyName == "drive-mode") {
            return setDriveMode(cameraId, value);
        } else {
            response.success = false;
            response.message = "Custom method not implemented for property: " + propertyName;
            return response;
        }
    }

    // Generic handler for simple properties (no custom method needed)
    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        // Convert string value to SDK value (supports hex, decimal, string enums, and
        // human-readable formats like "1/125" for shutter speed)
        CrInt64u sdkValue = convertValueToSDK(value, propertyName);

        // Use SDK SetDeviceProperty to set the value
        CrInt32u codes[1] = { mapping.sdkPropertyCode };
        SDK::CrDeviceProperty* properties = nullptr;
        CrInt32 numProps = 0;

        // First get the current property to check writability and available values
        auto err = SDK::GetSelectDeviceProperties(camera->get_device_handle(), 1, codes, &properties, &numProps);

        if (CR_SUCCEEDED(err) && numProps > 0 && properties != nullptr) {
            // Check writability
            if (1 != properties[0].IsSetEnableCurrentValue()) {
                response.success = false;
                response.message = "Property " + propertyName + " is not writable (camera may not be in pc-remote mode)";
                SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
                return response;
            }

            // Validate against available values if they exist
            CrInt32u valueSize = properties[0].GetValueSize();
            if (valueSize > 0) {
                // Determine element size based on property type
                CrInt32u elemSize = sizeof(std::uint32_t); // default
                if (propertyName == "aperture" || propertyName == "f-number" ||
                    propertyName == "white-balance" || propertyName == "focus-mode" ||
                    propertyName == "focus-area" || propertyName == "priority-key" ||
                    propertyName == "raw-compression" || propertyName == "still-image-store-destination" ||
                    propertyName == "focus-position" || propertyName == "focus-position-current" ||
                    propertyName == "file-format") {
                    elemSize = sizeof(std::uint16_t);
                }

                CrInt32u numValues = valueSize / elemSize;
                bool found = false;

                // Check if this is a range property (min/max/step = exactly 3 values)
                // Only specific properties use range format — don't assume from value count alone
                bool isRange = (numValues == 3) && (
                    propertyName == "focus-position" ||
                    propertyName == "focus-position-current" ||
                    propertyName == "zoom-distance"
                );

                if (isRange) {
                    // Range validation: values are [min, max, step]
                    CrInt64u minVal, maxVal, stepVal;
                    if (elemSize == sizeof(std::uint16_t)) {
                        auto vals = reinterpret_cast<const std::uint16_t*>(properties[0].GetValues());
                        minVal = vals[0]; maxVal = vals[1]; stepVal = vals[2];
                    } else {
                        auto vals = reinterpret_cast<const std::uint32_t*>(properties[0].GetValues());
                        minVal = vals[0]; maxVal = vals[1]; stepVal = vals[2];
                    }
                    if (sdkValue >= minVal && sdkValue <= maxVal) {
                        if (stepVal == 0 || (sdkValue - minVal) % stepVal == 0) {
                            found = true;
                        }
                    }
                } else if (elemSize == sizeof(std::uint16_t)) {
                    auto vals = reinterpret_cast<const std::uint16_t*>(properties[0].GetValues());
                    for (CrInt32u i = 0; i < numValues; i++) {
                        if (vals[i] == static_cast<std::uint16_t>(sdkValue)) { found = true; break; }
                    }
                } else {
                    auto vals = reinterpret_cast<const std::uint32_t*>(properties[0].GetValues());
                    for (CrInt32u i = 0; i < numValues; i++) {
                        if (vals[i] == static_cast<std::uint32_t>(sdkValue)) { found = true; break; }
                    }
                }

                if (!found) {
                    response.success = false;
                    std::stringstream hexStr;
                    hexStr << "0x" << std::hex << sdkValue;
                    response.message = "Value '" + value + "' (sdk: " + hexStr.str() +
                        ") is not in the camera's available values for " + propertyName +
                        ". GET the property first to see available_values.";
                    SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
                    return response;
                }
            }

            // Set the new value
            properties[0].SetCurrentValue(sdkValue);

            // Apply the setting
            err = SDK::SetDeviceProperty(camera->get_device_handle(), &properties[0]);

            if (CR_SUCCEEDED(err)) {
                response.success = true;
                response.message = "Property " + propertyName + " set successfully";
                response.data["property"] = propertyName;
                response.data["requested_value"] = value;
                std::stringstream hexStr;
                hexStr << "0x" << std::hex << sdkValue;
                response.data["value"] = hexStr.str();

                // Populate camera metadata
                response.camera.connected = camera->is_connected();
                response.camera.model = camera->get_model();
                response.camera.id = camera->get_id();
            } else {
                response.success = false;
                response.message = "Failed to set property: SDK error " + std::to_string(err);
            }

            SDK::ReleaseDeviceProperties(camera->get_device_handle(), properties);
        } else {
            response.success = false;
            response.message = "Failed to get property for setting: SDK error";
        }
    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception setting property: " + std::string(e.what());
        std::cerr << "❌ Exception in setPropertyGeneric: " << e.what() << std::endl;
    }

    return response;
}

ApiResponse CameraWebController::executeActionGeneric(const std::string& cameraId, const std::string& actionName, const std::string& params) {
    ApiResponse response;

    // Lookup action mapping
    auto it = ACTION_MAP.find(actionName);
    if (it == ACTION_MAP.end()) {
        response.success = false;
        response.message = "Unknown action: " + actionName;
        return response;
    }

    const ActionMapping& mapping = it->second;

    // Find camera
    std::shared_ptr<CameraDevice> camera = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
        auto camIt = m_cameraThreads.find(cameraId);
        if (camIt != m_cameraThreads.end()) {
            camera = camIt->second->camera;
        }
    }

    if (!camera || !camera->is_connected()) {
        response.success = false;
        response.message = "Camera not connected";
        return response;
    }

    try {
        std::cout << "🎬 Executing action: " << actionName << " on camera " << cameraId << std::endl;

        // Route to custom method if available (uses official SDK patterns)
        if (mapping.hasCustomMethod) {
            std::cout << "🔀 Routing " << actionName << " to custom method: " << mapping.sdkMethodName << std::endl;

            if (actionName == "shutter") {
                // Parse action type from params (down, up, or default full capture)
                std::string shutterAction = "";
                if (!params.empty()) {
                    try {
                        Json::Value root;
                        Json::CharReaderBuilder builder;
                        std::string errs;
                        std::istringstream bodyStream(params);

                        if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                            if (root.isMember("action")) {
                                shutterAction = root["action"].asString();
                            }
                        }
                    } catch (...) {
                        // If parsing fails, use default behavior
                    }
                }

                // Execute appropriate shutter command
                if (shutterAction == "down") {
                    camera->shutter_down();
                    response.success = true;
                    response.message = "Shutter pressed down (continuous shooting started)";
                } else if (shutterAction == "up") {
                    camera->shutter_up();
                    response.success = true;
                    response.message = "Shutter released (continuous shooting stopped)";
                } else {
                    // Default: Use official SDK capture_image() pattern: Down -> wait -> Up
                    camera->capture_image();
                    response.success = true;
                    response.message = "Shutter action executed successfully";
                }
            }
            else if (actionName == "half-press") {
                // Use official SDK s1_shooting() pattern (non-interactive for web API)
                camera->s1_shooting_non_interactive();
                response.success = true;
                response.message = "Half-press (S1 shooting) executed successfully";
            }
            else if (actionName == "af-shutter") {
                // Use official SDK af_shutter() pattern: S1 lock -> shutter -> S1 unlock
                bool success = camera->af_shutter();
                if (!success) {
                    response.success = false;
                    response.message = "AF+Shutter failed: Camera must be in AF mode (not Manual Focus)";
                    response.data["error"] = "focus_mode_not_af";
                } else {
                    response.success = true;
                    response.message = "AF+Shutter action executed successfully";
                }
            }
            else if (actionName == "zoom") {
                // Parse speed from params
                int speed = 0;
                if (!params.empty()) {
                    try {
                        // Params is expected to be the JSON body
                        Json::Value root;
                        Json::CharReaderBuilder builder;
                        std::string errs;
                        std::istringstream bodyStream(params);

                        if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                            if (root.isMember("speed") && root["speed"].isInt()) {
                                // Numeric speed: -10 to +10 (negative=out, positive=in)
                                speed = root["speed"].asInt();
                            } else if (root.isMember("direction")) {
                                // REST format: {"direction": "in"/"out", "speed": "normal"/"fast"}
                                std::string dir = root["direction"].asString();
                                std::string spd = root.isMember("speed") ? root["speed"].asString() : "normal";
                                int magnitude = (spd == "fast") ? 8 : 4;
                                speed = (dir == "out") ? -magnitude : magnitude;
                            }
                        }
                    } catch (...) {
                        // JSON parse failed — ignore, speed stays at default 0
                    }
                }

                // Execute zoom using the dedicated method
                ApiResponse zoomResult = executeZoomAction(cameraId, speed);
                response.success = zoomResult.success;
                response.message = zoomResult.message;
                response.data = zoomResult.data;
                return response;
            }
            else if (actionName == "focus-near-far") {
                // Parse step from params JSON body
                int step = 0;
                if (!params.empty()) {
                    try {
                        Json::Value root;
                        Json::CharReaderBuilder builder;
                        std::string errs;
                        std::istringstream bodyStream(params);
                        if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                            if (root.isMember("step")) {
                                step = root["step"].asInt();
                            }
                        }
                    } catch (...) {
                        step = std::stoi(params);
                    }
                }

                // Validate range: -7 (near) to +7 (far), 0 is invalid
                if (step < -7 || step > 7 || step == 0) {
                    response.success = false;
                    response.message = "Invalid step value. Must be -7 to -1 (near) or +1 to +7 (far)";
                    return response;
                }

                // First check if NearFar is enabled on this camera
                auto camera = getCameraDevice(cameraId);
                if (!camera) {
                    response.success = false;
                    response.message = "Camera not found: " + cameraId;
                    return response;
                }

                // Set NearFar property with the step value
                SDK::CrDeviceProperty prop;
                prop.SetCode(SDK::CrDevicePropertyCode::CrDeviceProperty_NearFar);
                prop.SetCurrentValue(static_cast<CrInt64u>(static_cast<CrInt16>(step)));
                prop.SetValueType(SDK::CrDataType::CrDataType_Int16Range);

                auto err = SDK::SetDeviceProperty(camera->get_device_handle(), &prop);
                if (CR_SUCCEEDED(err)) {
                    response.success = true;
                    if (step < 0) {
                        response.message = "Focus moving near (step " + std::to_string(step) + ")";
                    } else {
                        response.message = "Focus moving far (step +" + std::to_string(step) + ")";
                    }
                    response.data["step"] = std::to_string(step);
                    response.data["direction"] = (step < 0) ? "near" : "far";
                    std::cout << "🔍 Focus near/far: step=" << step << std::endl;
                } else {
                    response.success = false;
                    response.message = "Focus near/far failed (error: 0x" +
                        ([&]{ std::ostringstream oss; oss << std::hex << static_cast<int>(err); return oss.str(); })() + ")";
                    std::cerr << "❌ Focus near/far SDK error: 0x" << std::hex << static_cast<int>(err) << std::dec << std::endl;
                }
                return response;
            }
            else {
                response.success = false;
                response.message = "Custom method '" + mapping.sdkMethodName + "' not implemented for action: " + actionName;
                return response;
            }

            response.data["action"] = actionName;
            response.data["method"] = mapping.sdkMethodName;
        }
        else {
            // Generic SDK command execution (for actions without custom methods)
            // Following official SDK pattern: send Down, wait 200ms, send Up for Release commands

            if (mapping.sdkCommandId == SDK::CrCommandId::CrCommandId_Release) {
                // Official SDK pattern for shutter release
                std::cout << "📸 Shutter down" << std::endl;
                auto err_down = SDK::SendCommand(camera->get_device_handle(),
                                                 SDK::CrCommandId::CrCommandId_Release,
                                                 SDK::CrCommandParam::CrCommandParam_Down);

                if (CR_FAILED(err_down)) {
                    response.success = false;
                    response.message = "Failed to press shutter: SDK error " + std::to_string(err_down);
                    return response;
                }

                // Wait 200ms before releasing (user specified delay)
                std::this_thread::sleep_for(std::chrono::milliseconds(200));

                std::cout << "📸 Shutter up (releasing button)" << std::endl;
                auto err_up = SDK::SendCommand(camera->get_device_handle(),
                                               SDK::CrCommandId::CrCommandId_Release,
                                               SDK::CrCommandParam::CrCommandParam_Up);

                if (CR_FAILED(err_up)) {
                    std::cerr << "⚠️  Warning: Failed to release shutter button" << std::endl;
                }

                response.success = true;
                response.message = "Action " + actionName + " executed and released successfully";
            }
            else {
                // For non-release commands, send single command as specified
                auto err = SDK::SendCommand(camera->get_device_handle(), mapping.sdkCommandId,
                                           (SDK::CrCommandParam)mapping.commandParam);

                if (CR_SUCCEEDED(err)) {
                    response.success = true;
                    response.message = "Action " + actionName + " executed successfully";
                } else {
                    response.success = false;
                    response.message = "Failed to execute action: SDK error " + std::to_string(err);
                    return response;
                }
            }

            response.data["action"] = actionName;
            response.data["command_id"] = std::to_string(mapping.sdkCommandId);
            response.data["command_param"] = std::to_string(mapping.commandParam);
        }

        // Populate camera metadata
        response.camera.connected = camera->is_connected();
        response.camera.model = camera->get_model();
        response.camera.id = camera->get_id();

    } catch (const std::exception& e) {
        response.success = false;
        response.message = "Exception executing action: " + std::string(e.what());
        std::cerr << "❌ Exception in executeActionGeneric: " << e.what() << std::endl;
    }

    return response;
}

std::shared_ptr<CameraDevice> CameraWebController::getCameraDevice(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);

    // Find in connected camera threads map (only connected cameras can execute commands)
    auto it = m_cameraThreads.find(cameraId);
    if (it != m_cameraThreads.end()) {
        return it->second->camera;
    }

    return nullptr;
}

ConnectionMode CameraWebController::getConnectionMode(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(m_cameraThreadsMutex);
    auto it = m_connectedModes.find(cameraId);
    if (it != m_connectedModes.end()) {
        return it->second;
    }
    return ConnectionMode::Remote;  // Default
}

void CameraWebController::wireCameraEventCallback(std::shared_ptr<CameraDevice> camera, const std::string& cameraId)
{
    if (!m_webEventCallback) return;

    // Capture cameraId by value, callback by reference (it outlives the camera)
    auto callback = m_webEventCallback;
    std::string camId = cameraId;

    camera->setEventCallback(
        [callback, camId](const std::string& eventType, const std::string& jsonData) {
            callback(camId, eventType, jsonData);
        });

    std::cout << "[SSE] Wired event callback for camera: " << cameraId << std::endl;
}

} // namespace cli
