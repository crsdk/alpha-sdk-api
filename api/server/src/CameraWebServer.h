#ifndef CAMERAWEBSERVER_H
#define CAMERAWEBSERVER_H

#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <chrono>
#include <deque>
#include "CameraWebController.h"

#if defined(__APPLE__) || defined(__linux__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SSIZE_T ssize_t;
#define SHUT_RDWR SD_BOTH
#define MSG_NOSIGNAL 0
#define MSG_DONTWAIT 0
inline int close(SOCKET s) { return closesocket(s); }
#endif

namespace cli {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::string headers;
    std::string contentType;
};

struct HttpResponse {
    int statusCode;
    std::string statusText;
    std::string contentType;
    std::string body;
    
    HttpResponse() : statusCode(200), statusText("OK"), contentType("text/html") {}
};

struct WebSocketClient {
    int socket;
    bool connected;
    std::string streamingCameraId;  // which camera this client is streaming from

    WebSocketClient(int sock) : socket(sock), connected(true) {}
};

struct SSEClient {
    int socket;
    std::string cameraId;  // which camera to receive events for ("*" = all)
    std::atomic<bool> connected;

    SSEClient(int sock, const std::string& camId)
        : socket(sock), cameraId(camId), connected(true) {}
};

class CameraWebServer {
public:
    CameraWebServer(int port = 8080);
    ~CameraWebServer();
    
    bool start();
    void stop();
    bool isRunning() const { return m_running; }
    void addLog(const std::string& level, const std::string& message, const std::string& cameraId = "");
    
private:
    int m_port;
    int m_serverSocket;
    std::atomic<bool> m_running;
    std::thread m_serverThread;
    std::unique_ptr<CameraWebController> m_cameraController;
    
    // WebSocket support
    std::vector<std::shared_ptr<WebSocketClient>> m_wsClients;
    std::mutex m_clientsMutex;
    
    // Live view broadcasting
    std::atomic<bool> m_broadcastingLiveView;
    std::thread m_broadcastThread;

    // Server management
    std::chrono::steady_clock::time_point m_startTime;
    struct LogEntry {
        std::string timestamp;
        std::string level;
        std::string message;
        std::string cameraId;
    };
    std::deque<LogEntry> m_logBuffer;
    std::mutex m_logMutex;
    static const size_t MAX_LOG_ENTRIES = 1000;

    // Active client thread tracking
    std::atomic<int> m_activeClientCount{0};
    std::mutex m_clientCountMutex;
    std::condition_variable m_clientCountCV;

    // SSE (Server-Sent Events) support
    std::vector<std::shared_ptr<SSEClient>> m_sseClients;
    std::mutex m_sseClientsMutex;
    
    void serverLoop();
    void handleClient(int clientSocket);
    HttpRequest parseRequest(const std::string& request);
    HttpResponse handleRequest(const HttpRequest& request);
    std::string formatResponse(const HttpResponse& response);
    
    // SSE methods
    bool isSSERequest(const HttpRequest& request);
    void handleSSEConnection(int clientSocket, const HttpRequest& request);
    void sendSSEEvent(const std::string& cameraId, const std::string& eventType,
                      const std::string& jsonData);
    void removeSSEClient(int socket);

    // WebSocket methods
    bool isWebSocketUpgrade(const HttpRequest& request);
    void handleWebSocketUpgrade(int clientSocket, const HttpRequest& request);
    void handleWebSocketMessage(std::shared_ptr<WebSocketClient> client, const std::string& message);
    void broadcastToWebSocketClients(const std::string& message);
    void broadcastBinaryToWebSocketClients(const std::vector<uint8_t>& data);
    void removeWebSocketClient(int socket);
    std::string generateWebSocketKey(const std::string& clientKey);
    void startLiveViewBroadcasting();
    void stopLiveViewBroadcasting();
    void liveViewBroadcastThread();
    
    // API handlers
    HttpResponse handleApiStatus();
    HttpResponse handleApiConnect(const HttpRequest& request);
    HttpResponse handleApiDisconnect();
    HttpResponse handleApiCameras();
    HttpResponse handleApiDiscoverCameras();
    HttpResponse handleApiCapture();
    HttpResponse handleApiSetISO(const std::string& cameraId, const std::string& isoValue);
    HttpResponse handleApiSetAperture(const std::string& cameraId, const std::string& apertureValue);
    HttpResponse handleApiSetShutter(const std::string& cameraId, const std::string& shutterValue);
    HttpResponse handleApiSetWhiteBalance(const std::string& cameraId, const std::string& wbValue);

    // Camera property retrieval handlers
    HttpResponse handleApiGetCurrentISO(const std::string& cameraId);
    HttpResponse handleApiGetCurrentAperture(const std::string& cameraId);
    HttpResponse handleApiGetCurrentShutterSpeed(const std::string& cameraId);
    HttpResponse handleApiGetCurrentWhiteBalance(const std::string& cameraId);
    HttpResponse handleApiGetCurrentFocusMode(const std::string& cameraId);
    HttpResponse handleApiGetAllCurrentSettings();
    HttpResponse handleApiGetAllProperties(const std::string& cameraId);

    HttpResponse handleApiStartLiveView();
    HttpResponse handleApiStopLiveView(); 
    HttpResponse handleApiStartLiveViewStreaming();
    HttpResponse handleApiStopLiveViewStreaming();
    HttpResponse handleApiGetLiveViewFrame();
    HttpResponse handleApiAutoFocus();
    HttpResponse handleApiZoomIn();
    HttpResponse handleApiZoomOut();

    // OSD Image overlay handlers
    HttpResponse handleApiEnableOSD();
    HttpResponse handleApiDisableOSD();
    HttpResponse handleApiGetOSDStatus();
    HttpResponse handleApiGetCompositeFrame();
    HttpResponse handleStaticFile(const std::string& path);

    // Action handlers
    HttpResponse handleApiS1Shooting(const std::string& cameraId);
    HttpResponse handleApiAfShutter(const std::string& cameraId);
    HttpResponse handleApiToggleMovieRec(const std::string& cameraId);
    HttpResponse handleApiSetExposureMode(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiSetDriveMode(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiSetFocusMode(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiSetFocusArea(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiGetPriorityKey(const std::string& cameraId);
    HttpResponse handleApiSetPriorityKey(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiGetSaveDestination(const std::string& cameraId);
    HttpResponse handleApiSetSaveDestination(const std::string& cameraId, const HttpRequest& request);

    // Helper method to extract camera ID from path
    std::string extractCameraIdFromPath(const std::string& path) const;

    // NEW RESTful Generic Handlers (Scalable API design)
    HttpResponse handleApiConnectCamera(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiDisconnectCamera(const std::string& cameraId);
    HttpResponse handleApiGetConnectionStatus(const std::string& cameraId);
    HttpResponse handleApiGetPropertyGeneric(const std::string& cameraId, const std::string& propertyName);
    HttpResponse handleApiSetPropertyGeneric(const std::string& cameraId, const std::string& propertyName, const HttpRequest& request);
    HttpResponse handleApiExecuteActionGeneric(const std::string& cameraId, const std::string& actionName, const HttpRequest& request);

    // SD Card file transfer
    HttpResponse handleApiListSDCardFiles(const std::string& cameraId, const std::string& slotNumber);
    HttpResponse handleApiDownloadSDCardFile(const std::string& cameraId, const std::string& slotNumber, const std::string& contentId, const std::string& fileId, const HttpRequest& request);
    HttpResponse handleApiDownloadCompressed(const std::string& cameraId, const std::string& slotNumber, const std::string& contentId, const std::string& fileId, const std::string& type, const HttpRequest& request);

    // Per-camera live view handler
    HttpResponse handleApiLiveView(const std::string& cameraId, const std::string& action, const HttpRequest& request);

    // Camera settings file save/load handlers
    HttpResponse handleApiDownloadCameraSettings(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiUploadCameraSettings(const std::string& cameraId, const HttpRequest& request);
    HttpResponse handleApiListCameraSettings();
    HttpResponse handleApiImportLUT(const std::string& cameraId, const HttpRequest& request);

    // Server management endpoints
    HttpResponse handleApiServerStatus();
    HttpResponse handleApiServerLogs(const HttpRequest& request);
    HttpResponse handleApiServerShutdown();

};

} // namespace cli

#endif // CAMERAWEBSERVER_H
