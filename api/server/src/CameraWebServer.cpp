#include "CameraWebServer.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstring>
#include <regex>
#include <algorithm>
#include <chrono>
#include <errno.h>
#include <cstdint>
#include <json/json.h>
#include <vector>

namespace {
// Self-contained SHA-1 (RFC 3174) and Base64 — used only to compute the
// WebSocket accept-key during the upgrade handshake. This is a protocol
// formality (not encryption), so no crypto library is required.
void sha1(const unsigned char* data, size_t len, unsigned char out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    auto rol = [](uint32_t v, int b) { return (v << b) | (v >> (32 - b)); };
    std::vector<unsigned char> msg(data, data + len);
    uint64_t ml = static_cast<uint64_t>(len) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<unsigned char>((ml >> (i * 8)) & 0xFF));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (msg[off + i * 4] << 24) | (msg[off + i * 4 + 1] << 16) |
                   (msg[off + i * 4 + 2] << 8) | (msg[off + i * 4 + 3]);
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = static_cast<unsigned char>((h[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<unsigned char>((h[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<unsigned char>((h[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<unsigned char>(h[i] & 0xFF);
    }
}

std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(i + 1 < len ? tbl[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < len ? tbl[n & 63] : '=');
    }
    return out;
}
}  // namespace

namespace cli {

namespace {

bool isPlaceholderCameraId(const std::string& cameraId) {
    std::string normalized = cameraId;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized.empty() ||
           normalized == "{cameraid}" ||
           normalized == "%7bcameraid%7d";
}

HttpResponse invalidCameraIdResponse() {
    HttpResponse response;
    response.statusCode = 400;
    response.statusText = "Bad Request";
    response.body = R"({"success": false, "message": "Camera ID is required in the URL path. Replace {cameraId} with a real camera ID from GET /api/cameras."})";
    response.contentType = "application/json";
    return response;
}

} // namespace

CameraWebServer::CameraWebServer(int port)
    : m_port(port), m_serverSocket(-1), m_running(false), m_broadcastingLiveView(false),
      m_startTime(std::chrono::steady_clock::now())
{
    m_cameraController = std::make_unique<CameraWebController>();
}

CameraWebServer::~CameraWebServer() {
    stopLiveViewBroadcasting();
    stop();
}

bool CameraWebServer::start() {
    if (m_running) {
        std::cout << "Web server is already running" << std::endl;
        return true;
    }
    
    // Create socket
    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSocket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt))) {
        std::cerr << "Failed to set socket options" << std::endl;
        close(m_serverSocket);
        return false;
    }
    
    // Bind socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(m_port);
    
    if (bind(m_serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind socket to port " << m_port << std::endl;
        close(m_serverSocket);
        return false;
    }
    
    // Listen for connections
    if (listen(m_serverSocket, 10) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(m_serverSocket);
        return false;
    }
    
    // Check SDK initialized successfully before accepting connections
    if (!m_cameraController->isSDKInitialized()) {
        std::cerr << "Camera SDK failed to initialize — server will not start" << std::endl;
        close(m_serverSocket);
        m_serverSocket = -1;
        return false;
    }

    // Wire SSE event callback: controller → server → SSE clients
    m_cameraController->setEventCallback(
        [this](const std::string& cameraId, const std::string& eventType, const std::string& jsonData) {
            sendSSEEvent(cameraId, eventType, jsonData);
        });

    m_running = true;
    m_serverThread = std::thread(&CameraWebServer::serverLoop, this);

    std::cout << "Camera Web Server started on http://localhost:" << m_port << std::endl;
    std::cout << "SSE events available at /api/events and /api/cameras/{id}/events" << std::endl;
    std::cout << "Open this URL in your web browser to control the camera" << std::endl;
    
    return true;
}

void CameraWebServer::stop() {
    if (!m_running) return;

    std::cout << "[Shutdown] Stopping web server..." << std::endl;
    m_running = false;

    if (m_serverSocket >= 0) {
        close(m_serverSocket);
        m_serverSocket = -1;
    }

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    // Close all SSE client sockets to unblock their keepalive loops
    {
        std::lock_guard<std::mutex> lock(m_sseClientsMutex);
        for (auto& client : m_sseClients) {
            client->connected.store(false);
            shutdown(client->socket, SHUT_RDWR);
        }
    }

    // Close all WebSocket client sockets
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (auto& client : m_wsClients) {
            client->connected = false;
            shutdown(client->socket, SHUT_RDWR);
        }
    }

    // Wait up to 5 seconds for active client threads to finish
    {
        std::unique_lock<std::mutex> lock(m_clientCountMutex);
        if (m_activeClientCount.load() > 0) {
            std::cout << "[Shutdown] Waiting for " << m_activeClientCount.load() << " client thread(s) to finish..." << std::endl;
            m_clientCountCV.wait_for(lock, std::chrono::seconds(5), [this]() {
                return m_activeClientCount.load() == 0;
            });
            if (m_activeClientCount.load() > 0) {
                std::cout << "[Shutdown] Warning: " << m_activeClientCount.load() << " client thread(s) still active after timeout" << std::endl;
            }
        }
    }

    std::cout << "Camera Web Server stopped" << std::endl;
}

void CameraWebServer::serverLoop() {
    while (m_running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            if (m_running) {
                std::cerr << "Failed to accept client connection" << std::endl;
            }
            continue;
        }
        
        // Handle client in separate thread for better responsiveness
        m_activeClientCount.fetch_add(1);
        std::thread clientThread([this, clientSocket]() {
            handleClient(clientSocket);
            if (m_activeClientCount.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lock(m_clientCountMutex);
                m_clientCountCV.notify_all();
            }
        });
        clientThread.detach();
    }
}

void CameraWebServer::handleClient(int clientSocket) {
    char buffer[8192];
    ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead <= 0) {
        close(clientSocket);
        return;
    }

    buffer[bytesRead] = '\0';
    std::string requestStr(buffer, bytesRead);

    // Check if we have the full body based on Content-Length.
    // After a CORS preflight, the browser may deliver headers and body
    // in separate TCP segments, so a single recv() may only get headers.
    size_t headerEnd = requestStr.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
        // Extract Content-Length from headers
        size_t clPos = requestStr.find("Content-Length:");
        if (clPos == std::string::npos) clPos = requestStr.find("content-length:");
        if (clPos != std::string::npos && clPos < headerEnd) {
            size_t valStart = clPos + 15; // length of "Content-Length:"
            while (valStart < headerEnd && requestStr[valStart] == ' ') valStart++;
            size_t valEnd = requestStr.find("\r\n", valStart);
            int contentLength = std::stoi(requestStr.substr(valStart, valEnd - valStart));
            size_t bodyStart = headerEnd + 4; // past \r\n\r\n
            int bodyReceived = static_cast<int>(requestStr.size()) - static_cast<int>(bodyStart);

            // Read remaining body bytes if not fully received
            while (bodyReceived < contentLength) {
                int remaining = contentLength - bodyReceived;
                int toRead = (std::min)(remaining, static_cast<int>(sizeof(buffer) - 1));
                ssize_t n = recv(clientSocket, buffer, toRead, 0);
                if (n <= 0) break;
                requestStr.append(buffer, n);
                bodyReceived += n;
            }
        }
    }

    HttpRequest request = parseRequest(requestStr);

    // Debug: Log all requests to /ws path
    if (request.path == "/ws") {
        std::cout << "=== REQUEST TO /ws ===" << std::endl;
        std::cout << "Method: " << request.method << std::endl;
        std::cout << "Path: " << request.path << std::endl;
        std::cout << "Headers: " << request.headers.substr(0, 300) << std::endl;
        std::cout << "=== END REQUEST ===" << std::endl;
    }

    // Check if this is a WebSocket upgrade request
    if (isWebSocketUpgrade(request)) {
        handleWebSocketUpgrade(clientSocket, request);
        return;
    }

    // Check if this is an SSE (Server-Sent Events) request
    if (isSSERequest(request)) {
        handleSSEConnection(clientSocket, request);
        return;
    }

    // Handle normal HTTP request
    HttpResponse response = handleRequest(request);
    std::string responseStr = formatResponse(response);

    send(clientSocket, responseStr.c_str(), responseStr.length(), MSG_NOSIGNAL);
    close(clientSocket);
}

HttpRequest CameraWebServer::parseRequest(const std::string& request) {
    HttpRequest req;
    
    std::istringstream iss(request);
    std::string line;
    
    // Parse request line (GET /path HTTP/1.1)
    if (std::getline(iss, line)) {
        std::istringstream requestLine(line);
        std::string httpVersion;
        requestLine >> req.method >> req.path >> httpVersion;

        // Remove query parameters if present
        size_t queryPos = req.path.find('?');
        if (queryPos != std::string::npos) {
            req.path = req.path.substr(0, queryPos);
        }
    }
    
    // Parse headers (simple implementation)
    while (std::getline(iss, line) && line != "\r") {
        if (line.find("Content-Type:") == 0) {
            req.contentType = line.substr(14);
        }
        // Store headers for WebSocket detection
        req.headers += line + "\n";
    }
    
    // Parse body (for POST and PUT requests)
    if (req.method == "POST" || req.method == "PUT") {
        std::string bodyLine;
        req.body = "";
        while (std::getline(iss, bodyLine)) {
            req.body += bodyLine + "\n";
        }
        // Remove extra newline
        if (!req.body.empty() && req.body.back() == '\n') {
            req.body.pop_back();
        }
    }
    
    return req;
}

HttpResponse CameraWebServer::handleRequest(const HttpRequest& request) {
    std::cout << "Handling request: " << request.method << " " << request.path << std::endl;
    addLog("info", request.method + " " + request.path);

    // Handle CORS preflight requests
    if (request.method == "OPTIONS") {
        HttpResponse response;
        response.statusCode = 204;
        response.statusText = "No Content";
        response.contentType = "text/plain";
        response.body = "";
        return response;
    }

    // ==================================================================================
    // Server management routes
    // ==================================================================================

    if (request.path == "/api/server/status" && request.method == "GET") {
        return handleApiServerStatus();
    }
    if (request.path == "/api/server/logs" && request.method == "GET") {
        return handleApiServerLogs(request);
    }
    if (request.path == "/api/server/shutdown" && request.method == "POST") {
        return handleApiServerShutdown();
    }

    // ==================================================================================
    // NEW RESTFUL API ROUTES - Using regex matching to prevent route conflicts
    // These routes are checked FIRST to ensure proper matching order
    // ==================================================================================

    std::smatch matches;

    // Pattern: GET /api/cameras (enumerate/detect cameras)
    std::regex cameraListPattern(R"(^/api/cameras$)");
    if (std::regex_match(request.path, matches, cameraListPattern) && request.method == "GET") {
        return handleApiCameras();
    }

    // Pattern: POST /api/cameras/{cameraId}/connection (connect camera)
    // Pattern: DELETE /api/cameras/{cameraId}/connection (disconnect camera)
    // Pattern: GET /api/cameras/{cameraId}/connection (get connection status)
    std::regex connectionPattern(R"(^/api/cameras/([^/]+)/connection$)");
    if (std::regex_match(request.path, matches, connectionPattern)) {
        std::string cameraId = matches[1];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        if (request.method == "POST") {
            return handleApiConnectCamera(cameraId, request);
        } else if (request.method == "DELETE") {
            return handleApiDisconnectCamera(cameraId);
        } else if (request.method == "GET") {
            return handleApiGetConnectionStatus(cameraId);
        } else {
            // Method not allowed for this endpoint
            HttpResponse response;
            response.statusCode = 405;
            response.statusText = "Method Not Allowed";
            response.body = R"({"success": false, "message": "Method not allowed. Use POST, DELETE, or GET."})";
            response.contentType = "application/json";
            return response;
        }
    }

    // Pattern: GET/PUT /api/cameras/{cameraId}/properties/{property}
    std::regex propertyPattern(R"(^/api/cameras/([^/]+)/properties/([^/]+)$)");
    if (std::regex_match(request.path, matches, propertyPattern)) {
        std::string cameraId = matches[1];
        std::string propertyName = matches[2];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }

        // Special case: /properties/all -> get all properties
        if (propertyName == "all" && request.method == "GET") {
            return handleApiGetAllProperties(cameraId);
        }

        if (request.method == "GET") {
            return handleApiGetPropertyGeneric(cameraId, propertyName);
        } else if (request.method == "PUT") {
            return handleApiSetPropertyGeneric(cameraId, propertyName, request);
        } else {
            // Method not allowed for this endpoint
            HttpResponse response;
            response.statusCode = 405;
            response.statusText = "Method Not Allowed";
            response.body = R"({"success": false, "message": "Method not allowed. Use GET or PUT."})";
            response.contentType = "application/json";
            return response;
        }
    }

    // Pattern: POST /api/cameras/{cameraId}/actions/{action}
    std::regex actionPattern(R"(^/api/cameras/([^/]+)/actions/([^/]+)$)");
    if (std::regex_match(request.path, matches, actionPattern) && request.method == "POST") {
        std::string cameraId = matches[1];
        std::string actionName = matches[2];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiExecuteActionGeneric(cameraId, actionName, request);
    }

    // Pattern: GET /api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files (list files on SD card)
    std::regex sdCardListPattern(R"(^/api/cameras/([^/]+)/sd-card/slot/([12])/files$)");
    if (std::regex_match(request.path, matches, sdCardListPattern) && request.method == "GET") {
        std::string cameraId = matches[1];
        std::string slotNumber = matches[2];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiListSDCardFiles(cameraId, slotNumber);
    }

    // Pattern: POST /api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files/{contentId}/{fileId}/download (download file)
    std::regex sdCardDownloadPattern(R"(^/api/cameras/([^/]+)/sd-card/slot/([12])/files/(\d+)/(\d+)/download$)");
    if (std::regex_match(request.path, matches, sdCardDownloadPattern) && request.method == "POST") {
        std::string cameraId = matches[1];
        std::string slotNumber = matches[2];
        std::string contentId = matches[3];
        std::string fileId = matches[4];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiDownloadSDCardFile(cameraId, slotNumber, contentId, fileId, request);
    }

    // Pattern: POST /api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files/{contentId}/{fileId}/thumbnail (download thumbnail)
    // Pattern: POST /api/cameras/{cameraId}/sd-card/slot/{slotNumber}/files/{contentId}/{fileId}/screennail (download screennail)
    std::regex sdCardCompressedPattern(R"(^/api/cameras/([^/]+)/sd-card/slot/([12])/files/(\d+)/(\d+)/(thumbnail|screennail)$)");
    if (std::regex_match(request.path, matches, sdCardCompressedPattern) && request.method == "POST") {
        std::string cameraId = matches[1];
        std::string slotNumber = matches[2];
        std::string contentId = matches[3];
        std::string fileId = matches[4];
        std::string type = matches[5];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiDownloadCompressed(cameraId, slotNumber, contentId, fileId, type, request);
    }

    // Pattern: POST /api/cameras/{cameraId}/settings/download (save camera settings to PC)
    std::regex settingsDownloadPattern(R"(^/api/cameras/([^/]+)/settings/download$)");
    if (std::regex_match(request.path, matches, settingsDownloadPattern) && request.method == "POST") {
        std::string cameraId = matches[1];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiDownloadCameraSettings(cameraId, request);
    }

    // Pattern: POST /api/cameras/{cameraId}/settings/upload (load camera settings from PC)
    std::regex settingsUploadPattern(R"(^/api/cameras/([^/]+)/settings/upload$)");
    if (std::regex_match(request.path, matches, settingsUploadPattern) && request.method == "POST") {
        std::string cameraId = matches[1];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiUploadCameraSettings(cameraId, request);
    }

    // Pattern: GET /api/cameras/{cameraId}/settings/files (list saved camera settings files)
    std::regex settingsListPattern(R"(^/api/cameras/([^/]+)/settings/files$)");
    if (std::regex_match(request.path, matches, settingsListPattern) && request.method == "GET") {
        return handleApiListCameraSettings();
    }

    // Pattern: POST /api/cameras/{cameraId}/lut/import (import .cube LUT file to camera)
    std::regex lutImportPattern(R"(^/api/cameras/([^/]+)/lut/import$)");
    if (std::regex_match(request.path, matches, lutImportPattern) && request.method == "POST") {
        std::string cameraId = matches[1];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiImportLUT(cameraId, request);
    }

    // Pattern: /api/cameras/{cameraId}/live-view/{action}
    std::regex liveViewPattern(R"(^/api/cameras/([^/]+)/live-view/([^/]+)$)");
    if (std::regex_match(request.path, matches, liveViewPattern)) {
        std::string cameraId = matches[1];
        std::string action = matches[2];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiLiveView(cameraId, action, request);
    }

    // Pattern: GET/PUT /api/cameras/{cameraId}/settings/save-info (get/set save info: path, prefix, startNo)
    std::regex saveInfoPattern(R"(^/api/cameras/([^/]+)/settings/save-info$)");
    if (std::regex_match(request.path, matches, saveInfoPattern)) {
        std::string cameraId = matches[1];
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        if (request.method == "GET") {
            return handleApiGetSaveDestination(cameraId);
        } else if (request.method == "PUT") {
            return handleApiSetSaveDestination(cameraId, request);
        } else {
            HttpResponse response;
            response.statusCode = 405;
            response.statusText = "Method Not Allowed";
            response.body = R"({"success": false, "message": "Method not allowed. Use GET or PUT."})";
            response.contentType = "application/json";
            return response;
        }
    }

    // ==================================================================================
    // LEGACY API ROUTES - Kept for backward compatibility
    // These will eventually be deprecated in favor of RESTful routes above
    // ==================================================================================

    // Keep /api/status for backward compatibility
    if (request.path == "/api/status") {
        return handleApiStatus();
    } else if (request.path == "/api/cameras") {
        return handleApiCameras();
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/iso/") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        size_t isoPos = request.path.find("/properties/iso/");
        std::string isoValue = request.path.substr(isoPos + 16); // Remove "/properties/iso/"
        return handleApiSetISO(cameraId, isoValue);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/aperture/") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        size_t aperturePos = request.path.find("/properties/aperture/");
        std::string apertureValue = request.path.substr(aperturePos + 21); // Remove "/properties/aperture/"
        return handleApiSetAperture(cameraId, apertureValue);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/shutter/") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        size_t shutterPos = request.path.find("/properties/shutter/");
        std::string shutterValue = request.path.substr(shutterPos + 20); // Remove "/properties/shutter/"
        return handleApiSetShutter(cameraId, shutterValue);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/wb/") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        size_t wbPos = request.path.find("/properties/wb/");
        std::string wbValue = request.path.substr(wbPos + 15); // Remove "/properties/wb/"
        return handleApiSetWhiteBalance(cameraId, wbValue);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/iso") != std::string::npos && request.path.back() != '/') {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiGetCurrentISO(cameraId);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/aperture") != std::string::npos && request.path.back() != '/') {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiGetCurrentAperture(cameraId);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/shutterspeed") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiGetCurrentShutterSpeed(cameraId);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/whitebalance") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiGetCurrentWhiteBalance(cameraId);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/focusmode") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiGetCurrentFocusMode(cameraId);
    }
    // New action endpoints
    else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/actions/half-press") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiS1Shooting(cameraId);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/actions/af-shutter") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiAfShutter(cameraId);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/actions/movie-rec") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiToggleMovieRec(cameraId);
    }
    // New property setters
    else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/exposure-mode") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiSetExposureMode(cameraId, request);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/drive-mode") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiSetDriveMode(cameraId, request);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/focus-mode") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiSetFocusMode(cameraId, request);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/properties/focus-area") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        return handleApiSetFocusArea(cameraId, request);
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/priority-key") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        if (request.method == "GET") {
            return handleApiGetPriorityKey(cameraId);
        } else if (request.method == "PUT") {
            return handleApiSetPriorityKey(cameraId, request);
        }
    } else if (request.path.find("/api/cameras/") != std::string::npos && request.path.find("/settings/save-destination") != std::string::npos) {
        std::string cameraId = extractCameraIdFromPath(request.path);
        if (isPlaceholderCameraId(cameraId)) {
            return invalidCameraIdResponse();
        }
        if (request.method == "GET") {
            return handleApiGetSaveDestination(cameraId);
        } else if (request.method == "PUT") {
            return handleApiSetSaveDestination(cameraId, request);
        }
    }
    // Static file routes - serve React build files
    else if (request.path == "/" || request.path == "/index.html") {
        std::cout << "DEBUG: Root request detected, serving index.html" << std::endl;
        return handleStaticFile("index.html");
    } else if (request.path.find("/static/") == 0) {
        // Handle static assets (CSS, JS, etc.)
        std::string filePath = request.path.substr(1); // Remove leading slash
        return handleStaticFile(filePath);
    } else if (request.path == "/favicon.ico" ||
               request.path == "/manifest.json" ||
               request.path == "/robots.txt" ||
               request.path == "/logo192.png" ||
               request.path == "/logo512.png" ||
               request.path == "/sony-logo.png" ||
               request.path == "/asset-manifest.json") {
        // Handle other React build assets
        std::string filePath = request.path.substr(1); // Remove leading slash
        return handleStaticFile(filePath);
    }
    // 404 Not Found
    else {
        HttpResponse response;
        response.statusCode = 404;
        response.statusText = "Not Found";
        response.body = "<html><body><h1>404 - Page Not Found</h1></body></html>";
        return response;
    }

    // Unreachable — satisfies MSVC C4715
    HttpResponse fallback;
    fallback.statusCode = 500;
    return fallback;
}

HttpResponse CameraWebServer::handleApiStatus() {
    auto result = m_cameraController->getStatus();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiConnect(const HttpRequest& request) {
    std::string connectionMode = "remote-transfer";
    std::string cameraId = "";
    std::string username = "";
    std::string password = "";

    // Parse JSON body if present
    if (request.method == "POST" && !request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream iss(request.body);

            if (Json::parseFromStream(builder, iss, &root, &errs)) {
                if (root.isMember("connectionMode") && root["connectionMode"].isString()) {
                    connectionMode = root["connectionMode"].asString();
                }
                if (root.isMember("cameraId") && root["cameraId"].isString()) {
                    cameraId = root["cameraId"].asString();
                }
                if (root.isMember("username") && root["username"].isString()) {
                    username = root["username"].asString();
                }
                if (root.isMember("password") && root["password"].isString()) {
                    password = root["password"].asString();
                }
                std::cout << "Parsed connect request - Mode: " << connectionMode << ", CameraId: " << cameraId << std::endl;
            } else {
                std::cout << "Failed to parse JSON body: " << errs << std::endl;
            }
        } catch (const std::exception& e) {
            std::cout << "Exception parsing JSON: " << e.what() << std::endl;
        }
    }

    auto result = m_cameraController->connectCamera(connectionMode, cameraId, username, password);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiDisconnect() {
    auto result = m_cameraController->disconnectCamera();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiCameras() {
    // Trigger camera discovery - this will refresh the available cameras list
    auto cameras = m_cameraController->getAvailableCameras();

    // Create response with discovery status
    Json::Value responseJson;
    responseJson["success"] = true;
    responseJson["message"] = "Camera discovery completed";
    responseJson["cameras"] = Json::Value(Json::arrayValue);

    for (const auto& camera : cameras) {
        Json::Value cameraJson;
        cameraJson["id"] = camera.id;
        cameraJson["model"] = camera.model;
        cameraJson["connectionType"] = camera.connectionType;
        cameraJson["connected"] = camera.connected;
        responseJson["cameras"].append(cameraJson);
    }

    Json::StreamWriterBuilder builder;
    std::string jsonStr = Json::writeString(builder, responseJson);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = jsonStr;
    return response;
}

HttpResponse CameraWebServer::handleStaticFile(const std::string& path) {
    HttpResponse response;

    // Serve React build files from webapp/react/build/
    std::string buildPath = "../react/build/" + path;

    // Special case: if requesting root, serve index.html
    if (path == "index.html" || path.empty()) {
        buildPath = "../react/build/index.html";
    }

    std::cout << "handleStaticFile: requested path='" << path << "', buildPath='" << buildPath << "'" << std::endl;

    std::ifstream file(buildPath, std::ios::binary);
    if (file.is_open()) {
        // Determine content type based on file extension
        std::string ext = "";
        size_t dotPos = path.find_last_of(".");
        if (dotPos != std::string::npos) {
            ext = path.substr(dotPos + 1);
        }

        if (ext == "html") {
            response.contentType = "text/html";
        } else if (ext == "css") {
            response.contentType = "text/css";
        } else if (ext == "js") {
            response.contentType = "application/javascript";
        } else if (ext == "json") {
            response.contentType = "application/json";
        } else if (ext == "png") {
            response.contentType = "image/png";
        } else if (ext == "ico") {
            response.contentType = "image/x-icon";
        } else {
            response.contentType = "text/plain";
        }

        // Read file contents
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        response.body = content;
        response.statusCode = 200;
        response.statusText = "OK";

        file.close();
    } else {
        // File not found
        response.statusCode = 404;
        response.statusText = "Not Found";
        response.body = "<html><body><h1>404 - File Not Found</h1></body></html>";
    }

    return response;
}

std::string CameraWebServer::formatResponse(const HttpResponse& response) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << response.statusCode << " " << response.statusText << "\r\n";
    oss << "Content-Type: " << response.contentType << "\r\n";
    oss << "Content-Length: " << response.body.length() << "\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
    oss << "Access-Control-Allow-Headers: *\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << response.body;
    return oss.str();
}

// ============================================================================
// SSE (Server-Sent Events) implementation
// ============================================================================

bool CameraWebServer::isSSERequest(const HttpRequest& request) {
    if (request.method != "GET") return false;

    // Match: GET /api/events or GET /api/cameras/{id}/events
    if (request.path == "/api/events") return true;

    // Match /api/cameras/{id}/events
    std::regex eventsRegex("/api/cameras/([^/]+)/events");
    if (std::regex_match(request.path, eventsRegex)) return true;

    return false;
}

void CameraWebServer::handleSSEConnection(int clientSocket, const HttpRequest& request) {
    // Extract camera ID from path, or "*" for all cameras
    std::string cameraId = "*";
    std::regex cameraEventsRegex("/api/cameras/([^/]+)/events");
    std::smatch match;
    if (std::regex_match(request.path, match, cameraEventsRegex)) {
        cameraId = match[1].str();
    }

    std::cout << "[SSE] New client connected for camera: " << cameraId
              << " (socket " << clientSocket << ")" << std::endl;

    // Send SSE response headers — connection stays open
    std::string headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    if (send(clientSocket, headers.c_str(), headers.length(), MSG_NOSIGNAL) < 0) {
        close(clientSocket);
        return;
    }

    // Register this SSE client
    auto client = std::make_shared<SSEClient>(clientSocket, cameraId);
    {
        std::lock_guard<std::mutex> lock(m_sseClientsMutex);
        m_sseClients.push_back(client);
    }

    // Send initial connection event
    std::string connectEvent = "event: connected\ndata: {\"cameraId\":\"" + cameraId + "\"}\n\n";
    send(clientSocket, connectEvent.c_str(), connectEvent.length(), MSG_NOSIGNAL);

    // Keep thread alive — block until client disconnects or server stops
    // Check every 1s for shutdown; send keepalive every 30s
    int keepaliveCounter = 0;
    while (client->connected.load() && m_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        keepaliveCounter++;

        if (keepaliveCounter >= 30) {
            keepaliveCounter = 0;
            std::string keepalive = ": keepalive\n\n";
            ssize_t sent = send(clientSocket, keepalive.c_str(), keepalive.length(), MSG_NOSIGNAL);
            if (sent < 0) {
                break;  // client disconnected
            }
        }
    }

    // Cleanup
    removeSSEClient(clientSocket);
    close(clientSocket);
    std::cout << "[SSE] Client disconnected (socket " << clientSocket << ")" << std::endl;
}

void CameraWebServer::sendSSEEvent(const std::string& cameraId,
                                    const std::string& eventType,
                                    const std::string& jsonData) {
    std::string event = "event: " + eventType + "\ndata: " + jsonData + "\n\n";

    std::lock_guard<std::mutex> lock(m_sseClientsMutex);
    for (auto it = m_sseClients.begin(); it != m_sseClients.end(); ) {
        auto& client = *it;

        // Send if client subscribed to this camera or to all ("*")
        if (client->cameraId == cameraId || client->cameraId == "*") {
            ssize_t sent = send(client->socket, event.c_str(), event.length(), MSG_NOSIGNAL);
            if (sent < 0) {
                // Client disconnected — mark for cleanup
                client->connected.store(false);
                it = m_sseClients.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void CameraWebServer::removeSSEClient(int socket) {
    std::lock_guard<std::mutex> lock(m_sseClientsMutex);
    m_sseClients.erase(
        std::remove_if(m_sseClients.begin(), m_sseClients.end(),
                        [socket](const std::shared_ptr<SSEClient>& client) {
                            return client->socket == socket;
                        }),
        m_sseClients.end());
}

// ============================================================================
// WebSocket implementation
// ============================================================================

bool CameraWebServer::isWebSocketUpgrade(const HttpRequest& request) {
    bool isWebSocket = request.method == "GET" &&
           (request.headers.find("Upgrade: websocket") != std::string::npos ||
            request.headers.find("upgrade: websocket") != std::string::npos ||
            request.headers.find("Upgrade: WebSocket") != std::string::npos);

    if (isWebSocket) {
        std::cout << "WebSocket upgrade detected for path: " << request.path << std::endl;
        std::cout << "Request headers: " << request.headers.substr(0, 200) << "..." << std::endl;
    }

    return isWebSocket;
}

void CameraWebServer::handleWebSocketUpgrade(int clientSocket, const HttpRequest& request) {
    // Extract Sec-WebSocket-Key from headers
    std::string clientKey;
    size_t keyPos = request.headers.find("Sec-WebSocket-Key:");
    if (keyPos != std::string::npos) {
        size_t keyStart = keyPos + 18; // Length of "Sec-WebSocket-Key:"
        size_t keyEnd = request.headers.find("\r\n", keyStart);
        if (keyEnd == std::string::npos) {
            keyEnd = request.headers.find("\n", keyStart);
        }
        if (keyEnd != std::string::npos) {
            clientKey = request.headers.substr(keyStart, keyEnd - keyStart);
            // Trim whitespace
            clientKey.erase(0, clientKey.find_first_not_of(" \t"));
            clientKey.erase(clientKey.find_last_not_of(" \t\r\n") + 1);
        }
    }

    // Compute proper WebSocket accept key
    std::string acceptKey = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="; // Fallback
    if (!clientKey.empty()) {
        acceptKey = generateWebSocketKey(clientKey);
        std::cout << "Generated WebSocket accept key for client key: " << clientKey << std::endl;
    }

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
        "\r\n";

    send(clientSocket, response.c_str(), response.length(), MSG_NOSIGNAL);

    // Add client to WebSocket clients list
    auto wsClient = std::make_shared<WebSocketClient>(clientSocket);
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_wsClients.push_back(wsClient);
    }
    
    std::cout << "WebSocket client connected" << std::endl;
    
    // Send initial camera status
    auto status = m_cameraController->getStatus();
    std::string statusJson = "{ \"type\": \"camera_info\", \"connected\": " + 
                           std::string(status.camera.connected ? "true" : "false") + 
                           ", \"model\": \"" + status.camera.model + "\" }";
    
    // Send WebSocket text frame with proper length encoding
    {
        std::vector<uint8_t> wsFrame;
        wsFrame.push_back(0x81); // FIN=1, opcode=1 (text)
        size_t len = statusJson.length();
        if (len <= 125) {
            wsFrame.push_back(static_cast<uint8_t>(len));
        } else if (len <= 65535) {
            wsFrame.push_back(126);
            wsFrame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            wsFrame.push_back(static_cast<uint8_t>(len & 0xFF));
        } else {
            wsFrame.push_back(127);
            for (int i = 7; i >= 0; --i) {
                wsFrame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
            }
        }
        wsFrame.insert(wsFrame.end(), statusJson.begin(), statusJson.end());
        send(clientSocket, reinterpret_cast<const char*>(wsFrame.data()), wsFrame.size(), MSG_NOSIGNAL);
    }
    
    // Keep connection alive and handle messages with proper WebSocket frame parsing
    std::cout << "Starting WebSocket message loop for client" << std::endl;
    std::vector<uint8_t> recvBuf(4096);
    while (m_running && wsClient->connected) {
        // Read at least 2 bytes for the frame header
        ssize_t bytesRead = recv(clientSocket, reinterpret_cast<char*>(recvBuf.data()), recvBuf.size(), MSG_DONTWAIT);
        if (bytesRead > 0) {
            size_t pos = 0;
            while (pos < (size_t)bytesRead) {
                if ((size_t)bytesRead - pos < 2) break; // need at least 2 bytes

                uint8_t byte0 = recvBuf[pos];
                uint8_t byte1 = recvBuf[pos + 1];
                uint8_t opcode = byte0 & 0x0F;
                bool masked = (byte1 & 0x80) != 0;
                uint64_t payloadLen = byte1 & 0x7F;
                size_t headerLen = 2;

                if (payloadLen == 126) {
                    if ((size_t)bytesRead - pos < 4) break;
                    payloadLen = ((uint64_t)recvBuf[pos + 2] << 8) | recvBuf[pos + 3];
                    headerLen = 4;
                } else if (payloadLen == 127) {
                    if ((size_t)bytesRead - pos < 10) break;
                    payloadLen = 0;
                    for (int i = 0; i < 8; i++) {
                        payloadLen = (payloadLen << 8) | recvBuf[pos + headerLen + i];
                    }
                    headerLen = 10;
                }

                size_t maskLen = masked ? 4 : 0;
                size_t totalFrameLen = headerLen + maskLen + payloadLen;
                if ((size_t)bytesRead - pos < totalFrameLen) break; // incomplete frame

                // Handle close frame (opcode 0x8)
                if (opcode == 0x8) {
                    std::cout << "WebSocket client sent close frame" << std::endl;
                    // Send close frame back
                    uint8_t closeFrame[2] = {0x88, 0x00};
                    send(clientSocket, reinterpret_cast<const char*>(closeFrame), 2, MSG_NOSIGNAL);
                    wsClient->connected = false;
                    break;
                }

                // Handle ping (opcode 0x9) — reply with pong
                if (opcode == 0x9) {
                    uint8_t pongFrame[2] = {0x8A, 0x00};
                    send(clientSocket, reinterpret_cast<const char*>(pongFrame), 2, MSG_NOSIGNAL);
                    pos += totalFrameLen;
                    continue;
                }

                // Extract and unmask payload for text (0x1) and binary (0x2) frames
                if (opcode == 0x1 || opcode == 0x2) {
                    std::vector<uint8_t> payload(payloadLen);
                    size_t payloadStart = pos + headerLen + maskLen;
                    std::memcpy(payload.data(), &recvBuf[payloadStart], payloadLen);

                    if (masked) {
                        uint8_t maskKey[4];
                        std::memcpy(maskKey, &recvBuf[pos + headerLen], 4);
                        for (uint64_t i = 0; i < payloadLen; i++) {
                            payload[i] ^= maskKey[i % 4];
                        }
                    }

                    std::string message(payload.begin(), payload.end());
                    std::cout << "Received WebSocket message (" << payloadLen << " bytes): " << message.substr(0, 100) << std::endl;
                    handleWebSocketMessage(wsClient, message);
                }

                pos += totalFrameLen;
            }
        } else if (bytesRead == 0) {
            std::cout << "WebSocket client closed connection (bytesRead=0)" << std::endl;
            break;
        } else if (bytesRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cout << "WebSocket recv error: " << strerror(errno) << " (errno: " << errno << ")" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "WebSocket message loop ended, connected: " << wsClient->connected << std::endl;
    
    removeWebSocketClient(clientSocket);
    close(clientSocket);
}

void CameraWebServer::handleWebSocketMessage(std::shared_ptr<WebSocketClient> client, const std::string& message) {
    std::cout << "Received WebSocket message: " << message << std::endl;
    
    // Parse simple JSON commands
    if (message.find("connect") != std::string::npos) {
        auto result = m_cameraController->connectCamera();
        std::string response = "{ \"type\": \"camera_info\", \"connected\": " + 
                             std::string(result.camera.connected ? "true" : "false") + 
                             ", \"model\": \"" + result.camera.model + "\" }";
        
        // Broadcast to all WebSocket clients
        broadcastToWebSocketClients(response);
    }
    else if (message.find("disconnect") != std::string::npos) {
        auto result = m_cameraController->disconnectCamera();
        std::string response = "{ \"type\": \"camera_info\", \"connected\": false, \"model\": \"\" }";
        broadcastToWebSocketClients(response);
    }
    else if (message.find("start_liveview") != std::string::npos) {
        // Per-camera live view: parse cameraId from JSON if present
        std::string cameraId;
        size_t cidPos = message.find("\"cameraId\"");
        if (cidPos != std::string::npos) {
            size_t valStart = message.find("\"", cidPos + 10);
            if (valStart != std::string::npos) {
                valStart++; // skip opening quote
                size_t valEnd = message.find("\"", valStart);
                if (valEnd != std::string::npos) {
                    cameraId = message.substr(valStart, valEnd - valStart);
                }
            }
        }

        if (!cameraId.empty()) {
            // Per-camera streaming
            auto result = m_cameraController->startLiveViewStreaming(cameraId);
            std::string response = "{ \"type\": \"liveview_streaming\", \"started\": " +
                                 std::string(result.success ? "true" : "false") +
                                 ", \"cameraId\": \"" + cameraId +
                                 "\", \"message\": \"" + result.message + "\" }";
            // Set this client's streaming camera
            client->streamingCameraId = cameraId;
            broadcastToWebSocketClients(response);

            if (result.success) {
                startLiveViewBroadcasting();
            }
        } else {
            // Legacy: start_liveview_streaming without cameraId
            auto result = m_cameraController->startLiveViewStreaming();
            std::string response = "{ \"type\": \"liveview_streaming\", \"started\": " +
                                 std::string(result.success ? "true" : "false") +
                                 ", \"message\": \"" + result.message + "\" }";
            broadcastToWebSocketClients(response);

            if (result.success) {
                startLiveViewBroadcasting();
            }
        }
    }
    else if (message.find("stop_liveview") != std::string::npos) {
        // Per-camera stop: parse cameraId
        std::string cameraId;
        size_t cidPos = message.find("\"cameraId\"");
        if (cidPos != std::string::npos) {
            size_t valStart = message.find("\"", cidPos + 10);
            if (valStart != std::string::npos) {
                valStart++;
                size_t valEnd = message.find("\"", valStart);
                if (valEnd != std::string::npos) {
                    cameraId = message.substr(valStart, valEnd - valStart);
                }
            }
        }

        if (!cameraId.empty()) {
            auto result = m_cameraController->stopLiveViewStreaming(cameraId);
            std::string response = "{ \"type\": \"liveview_streaming\", \"started\": false, \"cameraId\": \"" +
                                 cameraId + "\", \"message\": \"" + result.message + "\" }";
            client->streamingCameraId.clear();
            broadcastToWebSocketClients(response);
        } else {
            auto result = m_cameraController->stopLiveViewStreaming();
            std::string response = "{ \"type\": \"liveview_streaming\", \"started\": false, \"message\": \"" + result.message + "\" }";
            broadcastToWebSocketClients(response);
            stopLiveViewBroadcasting();
        }
    }
}

void CameraWebServer::broadcastToWebSocketClients(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);

    // Build proper WebSocket text frame with correct length encoding
    std::vector<uint8_t> wsFrame;
    wsFrame.push_back(0x81); // FIN=1, opcode=1 (text)
    size_t len = message.length();
    if (len <= 125) {
        wsFrame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        wsFrame.push_back(126);
        wsFrame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        wsFrame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        wsFrame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            wsFrame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    wsFrame.insert(wsFrame.end(), message.begin(), message.end());
    std::string wsMessage(wsFrame.begin(), wsFrame.end());
    
    for (auto it = m_wsClients.begin(); it != m_wsClients.end();) {
        auto client = *it;
        if (client->connected) {
            ssize_t sent = send(client->socket, wsMessage.c_str(), wsMessage.length(), MSG_NOSIGNAL);
            if (sent <= 0) {
                client->connected = false;
                close(client->socket);
                it = m_wsClients.erase(it);
            } else {
                ++it;
            }
        } else {
            it = m_wsClients.erase(it);
        }
    }
}

void CameraWebServer::removeWebSocketClient(int socket) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    
    m_wsClients.erase(
        std::remove_if(m_wsClients.begin(), m_wsClients.end(),
                      [socket](const auto& client) { 
                          return client->socket == socket; 
                      }),
        m_wsClients.end()
    );
    
    std::cout << "WebSocket client disconnected" << std::endl;
}

// New API handlers
HttpResponse CameraWebServer::handleApiCapture() {
    auto result = m_cameraController->captureImage();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiSetISO(const std::string& cameraId, const std::string& isoValue) {
    auto result = m_cameraController->setISO(cameraId, isoValue);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiSetAperture(const std::string& cameraId, const std::string& apertureValue) {
    auto result = m_cameraController->setAperture(cameraId, apertureValue);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiSetShutter(const std::string& cameraId, const std::string& shutterValue) {
    auto result = m_cameraController->setShutterSpeed(cameraId, shutterValue);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiSetWhiteBalance(const std::string& cameraId, const std::string& wbValue) {
    auto result = m_cameraController->setWhiteBalance(cameraId, wbValue);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

// Camera property retrieval handlers
HttpResponse CameraWebServer::handleApiGetCurrentISO(const std::string& cameraId) {
    auto result = m_cameraController->getCurrentISO(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiGetCurrentAperture(const std::string& cameraId) {
    auto result = m_cameraController->getCurrentAperture(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiGetCurrentShutterSpeed(const std::string& cameraId) {
    auto result = m_cameraController->getCurrentShutterSpeed(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiGetCurrentWhiteBalance(const std::string& cameraId) {
    auto result = m_cameraController->getCurrentWhiteBalance(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiGetCurrentFocusMode(const std::string& cameraId) {
    auto result = m_cameraController->getCurrentFocusMode(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiGetAllCurrentSettings() {
    auto result = m_cameraController->getAllCurrentSettings();

    HttpResponse response;
    response.contentType = "application/json";
    response.body = result.success ? result.message : m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiGetAllProperties(const std::string& cameraId) {
    std::cout << "📦 RESTful Get All Properties: GET /api/cameras/" << cameraId << "/properties/all" << std::endl;

    auto result = m_cameraController->getAllProperties(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiStartLiveView() {
    auto result = m_cameraController->startLiveView();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiStopLiveView() {
    auto result = m_cameraController->stopLiveView();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiAutoFocus() {
    auto result = m_cameraController->autoFocus();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiZoomIn() {
    auto result = m_cameraController->zoomIn();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiZoomOut() {
    auto result = m_cameraController->zoomOut();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiStartLiveViewStreaming() {
    auto result = m_cameraController->startLiveViewStreaming();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiStopLiveViewStreaming() {
    auto result = m_cameraController->stopLiveViewStreaming();
    
    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    return response;
}

HttpResponse CameraWebServer::handleApiGetLiveViewFrame() {
    static int requestCount = 0;
    auto requestTime = std::chrono::steady_clock::now();
    static auto lastRequestTime = requestTime;

    requestCount++;
    auto timeSinceLastRequest = std::chrono::duration_cast<std::chrono::milliseconds>(requestTime - lastRequestTime).count();

    std::cout << "=== FRAME REQUEST #" << requestCount << " ===" << std::endl;
    std::cout << "Time since last request: " << timeSinceLastRequest << "ms" << std::endl;

    std::vector<uint8_t> jpegData;
    bool hasFrame = m_cameraController->getLiveViewFrame(jpegData);

    std::cout << "Frame available: " << (hasFrame ? "YES" : "NO")
              << ", Data size: " << jpegData.size() << " bytes" << std::endl;

    if (hasFrame && !jpegData.empty()) {
        // Validate JPEG data before sending
        bool isValidJPEG = false;
        if (jpegData.size() >= 4) {
            isValidJPEG = (jpegData[0] == 0xFF && jpegData[1] == 0xD8);
        }

        std::cout << "JPEG validation: " << (isValidJPEG ? "VALID" : "INVALID");
        if (jpegData.size() >= 4) {
            std::cout << " [0x" << std::hex << (int)jpegData[0] << ", 0x" << (int)jpegData[1] << "]";
        }
        std::cout << std::dec << std::endl;
    }

    HttpResponse response;
    if (hasFrame && !jpegData.empty()) {
        response.contentType = "image/jpeg";
        response.body = std::string(jpegData.begin(), jpegData.end());
        std::cout << "SUCCESS: Returning JPEG frame (" << jpegData.size() << " bytes)" << std::endl;
    } else {
        // Instead of 204 No Content (which triggers CORB), return 404 with proper CORS headers
        response.statusCode = 404;
        response.statusText = "Not Found";
        response.contentType = "text/plain";
        response.body = "No live view frame available";
        std::cout << "RESPONSE: 404 - No frame available" << std::endl;
    }

    lastRequestTime = requestTime;
    std::cout << "=========================" << std::endl;
    return response;
}

std::string CameraWebServer::generateWebSocketKey(const std::string& clientKey) {
    // WebSocket magic string as defined in RFC 6455
    const std::string magicString = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = clientKey + magicString;

    // SHA-1 of (key + magic), then Base64 — the RFC 6455 accept-key.
    unsigned char sha1Hash[20];
    sha1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), sha1Hash);
    return base64_encode(sha1Hash, 20);
}




// WebSocket binary streaming implementation
void CameraWebServer::broadcastBinaryToWebSocketClients(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_clientsMutex);

    if (data.empty()) return;

    // Validate JPEG data before sending
    bool isValidJPEG = false;
    if (data.size() >= 4) {
        isValidJPEG = (data[0] == 0xFF && data[1] == 0xD8);
        if (isValidJPEG) {
            // Log JPEG validation info
            static int frameCount = 0;
            static auto lastLogTime = std::chrono::steady_clock::now();
            frameCount++;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLogTime).count();
            if (elapsed >= 1000) {
                float fps = (frameCount * 1000.0f) / elapsed;
                std::cout << "WebSocket broadcast: " << frameCount << " frames, "
                          << fps << " fps, size: " << data.size() << " bytes, "
                          << "JPEG: [0x" << std::hex << (int)data[0] << ", 0x" << (int)data[1]
                          << "] ... [0x" << (int)data[data.size()-2] << ", 0x" << (int)data[data.size()-1]
                          << "]" << std::dec << std::endl;
                frameCount = 0;
                lastLogTime = now;
            }
        } else {
            std::cout << "WARNING: Broadcasting non-JPEG data! First bytes: 0x"
                      << std::hex << (int)data[0] << " 0x" << (int)data[1] << std::dec << std::endl;
        }
    }

    // Create proper WebSocket binary frame
    std::vector<uint8_t> frame;
    size_t dataSize = data.size();

    // WebSocket frame header
    frame.push_back(0x82); // FIN=1, opcode=2 (binary frame)

    // Payload length encoding
    if (dataSize <= 125) {
        frame.push_back(static_cast<uint8_t>(dataSize));
    } else if (dataSize <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((dataSize >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(dataSize & 0xFF));
    } else {
        frame.push_back(127);
        // 64-bit length in network byte order
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((dataSize >> (i * 8)) & 0xFF));
        }
    }

    // Add the JPEG payload
    frame.insert(frame.end(), data.begin(), data.end());

    // Broadcast to all connected WebSocket clients
    for (auto it = m_wsClients.begin(); it != m_wsClients.end();) {
        auto client = *it;
        if (client->connected) {
            ssize_t sent = send(client->socket, reinterpret_cast<const char*>(frame.data()), frame.size(), MSG_NOSIGNAL);
            if (sent <= 0) {
                std::cout << "WebSocket client disconnected during binary broadcast (errno: " << errno << ")" << std::endl;
                client->connected = false;
                close(client->socket);
                it = m_wsClients.erase(it);
            } else {
                ++it;
            }
        } else {
            it = m_wsClients.erase(it);
        }
    }
}

void CameraWebServer::startLiveViewBroadcasting() {
    if (m_broadcastingLiveView.load()) {
        std::cout << "Live view broadcasting already active" << std::endl;
        return;
    }
    
    m_broadcastingLiveView = true;
    m_broadcastThread = std::thread(&CameraWebServer::liveViewBroadcastThread, this);
    std::cout << "Started live view WebSocket broadcasting" << std::endl;
}

void CameraWebServer::stopLiveViewBroadcasting() {
    if (!m_broadcastingLiveView.load()) {
        return;
    }
    
    m_broadcastingLiveView = false;
    if (m_broadcastThread.joinable()) {
        m_broadcastThread.join();
    }
    std::cout << "Stopped live view WebSocket broadcasting" << std::endl;
}

void CameraWebServer::liveViewBroadcastThread() {
    std::cout << "Live view broadcast thread started" << std::endl;

    while (m_broadcastingLiveView.load() && m_running.load()) {
        std::lock_guard<std::mutex> lock(m_clientsMutex);

        if (m_wsClients.empty()) {
            // No clients, just sleep
        } else {
            // Collect unique cameraIds from connected WS clients
            std::unordered_set<std::string> activeCameraIds;
            bool hasLegacyClients = false;
            for (auto& client : m_wsClients) {
                if (client->connected) {
                    if (!client->streamingCameraId.empty()) {
                        activeCameraIds.insert(client->streamingCameraId);
                    } else {
                        hasLegacyClients = true;
                    }
                }
            }

            // Per-camera frame broadcast
            for (const auto& camId : activeCameraIds) {
                std::vector<uint8_t> jpegData;
                if (m_cameraController->getLiveViewFrame(camId, jpegData) && !jpegData.empty()) {
                    // Build WebSocket binary frame
                    std::vector<uint8_t> frame;
                    size_t dataSize = jpegData.size();
                    frame.push_back(0x82); // FIN=1, opcode=2 (binary)
                    if (dataSize <= 125) {
                        frame.push_back(static_cast<uint8_t>(dataSize));
                    } else if (dataSize <= 65535) {
                        frame.push_back(126);
                        frame.push_back(static_cast<uint8_t>((dataSize >> 8) & 0xFF));
                        frame.push_back(static_cast<uint8_t>(dataSize & 0xFF));
                    } else {
                        frame.push_back(127);
                        for (int i = 7; i >= 0; --i) {
                            frame.push_back(static_cast<uint8_t>((dataSize >> (i * 8)) & 0xFF));
                        }
                    }
                    frame.insert(frame.end(), jpegData.begin(), jpegData.end());

                    // Send only to clients subscribed to this camera
                    for (auto it = m_wsClients.begin(); it != m_wsClients.end();) {
                        auto& client = *it;
                        if (client->connected && client->streamingCameraId == camId) {
                            ssize_t sent = send(client->socket, reinterpret_cast<const char*>(frame.data()), frame.size(), MSG_NOSIGNAL);
                            if (sent <= 0) {
                                client->connected = false;
                                close(client->socket);
                                it = m_wsClients.erase(it);
                                continue;
                            }
                        }
                        ++it;
                    }
                }
            }

            // Legacy broadcast for clients without a streamingCameraId
            if (hasLegacyClients) {
                std::vector<uint8_t> jpegData;
                if (m_cameraController->getLiveViewFrame(jpegData) && !jpegData.empty()) {
                    broadcastBinaryToWebSocketClients(jpegData);
                }
            }
        }

        // Target ~15fps by sleeping for ~66ms
        std::this_thread::sleep_for(std::chrono::milliseconds(66));
    }

    std::cout << "Live view broadcast thread stopped" << std::endl;
}

// OSD Image overlay API handlers
HttpResponse CameraWebServer::handleApiEnableOSD() {
    std::cout << "🔵 OSD: Enable endpoint called" << std::endl;
    HttpResponse response;
    auto result = m_cameraController->enableOSDMode();

    response.statusCode = result.success ? 200 : 400;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    std::cout << "🔵 OSD: Enable endpoint completed, success=" << result.success << std::endl;

    return response;
}

HttpResponse CameraWebServer::handleApiDisableOSD() {
    std::cout << "🔵 OSD: Disable endpoint called" << std::endl;
    HttpResponse response;
    auto result = m_cameraController->disableOSDMode();

    response.statusCode = result.success ? 200 : 400;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    std::cout << "🔵 OSD: Disable endpoint completed, success=" << result.success << std::endl;

    return response;
}

HttpResponse CameraWebServer::handleApiGetOSDStatus() {
    std::cout << "🔵 OSD: Status endpoint called" << std::endl;
    HttpResponse response;
    auto result = m_cameraController->getOSDStatus();

    response.statusCode = result.success ? 200 : 400;
    response.contentType = "application/json";
    response.body = result.success ? result.message : m_cameraController->toJson(result);
    std::cout << "🔵 OSD: Status endpoint completed, success=" << result.success << std::endl;

    return response;
}

HttpResponse CameraWebServer::handleApiGetCompositeFrame() {
    HttpResponse response;
    std::vector<uint8_t> jpegData;

    bool success = m_cameraController->getCompositeFrame(jpegData);

    if (success && !jpegData.empty()) {
        response.statusCode = 200;
        response.contentType = "image/jpeg";
        response.body = std::string(reinterpret_cast<const char*>(jpegData.data()), jpegData.size());
    } else {
        response.statusCode = 404;
        response.contentType = "application/json";
        response.body = "{\"success\": false, \"message\": \"No composite frame available\"}";
    }

    return response;
}

// ============================================================================
// Per-Camera Live View Handler
// ============================================================================

HttpResponse CameraWebServer::handleApiLiveView(const std::string& cameraId, const std::string& action, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    if (action == "enable" && request.method == "POST") {
        auto result = m_cameraController->enableLiveView(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "disable" && request.method == "POST") {
        auto result = m_cameraController->disableLiveView(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "status" && request.method == "GET") {
        auto result = m_cameraController->getLiveViewStatus(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "start" && request.method == "POST") {
        auto result = m_cameraController->startLiveViewStreaming(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "stop" && request.method == "POST") {
        auto result = m_cameraController->stopLiveViewStreaming(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "frame" && request.method == "GET") {
        std::vector<uint8_t> jpegData;
        bool hasFrame = m_cameraController->getLiveViewFrame(cameraId, jpegData);
        if (hasFrame && !jpegData.empty()) {
            response.statusCode = 200;
            response.contentType = "image/jpeg";
            response.body = std::string(reinterpret_cast<const char*>(jpegData.data()), jpegData.size());
        } else {
            response.statusCode = 404;
            response.body = R"({"success": false, "message": "No live view frame available"})";
        }
    } else if (action == "osd-enable" && request.method == "POST") {
        auto result = m_cameraController->enableOSD(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "osd-disable" && request.method == "POST") {
        auto result = m_cameraController->disableOSD(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "osd-status" && request.method == "GET") {
        auto result = m_cameraController->getOSDStatus(cameraId);
        response.statusCode = result.success ? 200 : 400;
        response.body = m_cameraController->toJson(result);
    } else if (action == "osd-frame" && request.method == "GET") {
        std::vector<uint8_t> jpegData;
        bool hasFrame = m_cameraController->getCompositeFrame(cameraId, jpegData);
        if (hasFrame && !jpegData.empty()) {
            response.statusCode = 200;
            response.contentType = "image/jpeg";
            response.body = std::string(reinterpret_cast<const char*>(jpegData.data()), jpegData.size());
        } else {
            response.statusCode = 404;
            response.body = R"({"success": false, "message": "No OSD composite frame available. Ensure live view is streaming and OSD is enabled."})";
        }
    } else {
        response.statusCode = 400;
        response.body = R"({"success": false, "message": "Unknown live-view action: )" + action + R"( or method not allowed"})";
    }

    return response;
}

// ============================================================================
// New REST API Handler Implementations
// ============================================================================

std::string CameraWebServer::extractCameraIdFromPath(const std::string& path) const {
    // Extract camera ID from path like /api/cameras/{cameraId}/properties
    std::string prefix = "/api/cameras/";
    size_t startPos = path.find(prefix);
    if (startPos != std::string::npos) {
        startPos += prefix.length();
        size_t endPos = path.find("/", startPos);
        if (endPos != std::string::npos) {
            return path.substr(startPos, endPos - startPos);
        }
    }
    return "";
}

HttpResponse CameraWebServer::handleApiS1Shooting(const std::string& cameraId) {
    auto result = m_cameraController->s1Shooting(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiAfShutter(const std::string& cameraId) {
    auto result = m_cameraController->afShutter(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiToggleMovieRec(const std::string& cameraId) {
    auto result = m_cameraController->toggleMovieRecording(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiGetPriorityKey(const std::string& cameraId) {
    auto result = m_cameraController->getPriorityKey(cameraId);

    HttpResponse response;
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiSetPriorityKey(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    std::string setting = "";

    // Parse JSON body to get priority key setting
    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::istringstream bodyStream(request.body);
            std::string errs;

            if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                if (root.isMember("setting")) {
                    setting = root["setting"].asString();
                }
            }
        } catch (const std::exception& e) {
            response.statusCode = 400;
            response.contentType = "application/json";
            response.body = "{\"success\": false, \"message\": \"Invalid JSON body\"}";
            return response;
        }
    }

    if (setting.empty()) {
        response.statusCode = 400;
        response.contentType = "application/json";
        response.body = "{\"success\": false, \"message\": \"Missing 'setting' in request body\"}";
        return response;
    }

    auto result = m_cameraController->setPriorityKey(cameraId, setting);

    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiSetExposureMode(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    std::string mode = "";

    // Parse JSON body to get exposure mode
    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream iss(request.body);

            if (Json::parseFromStream(builder, iss, &root, &errs)) {
                if (root.isMember("mode") && root["mode"].isString()) {
                    mode = root["mode"].asString();
                }
            }
        } catch (const std::exception& e) {
            response.statusCode = 400;
            response.contentType = "application/json";
            response.body = "{\"error\": \"Invalid JSON in request body\"}";
            return response;
        }
    }

    auto result = m_cameraController->setExposureMode(cameraId, mode);
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiSetDriveMode(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    std::string mode = "";

    // Parse JSON body to get drive mode
    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream iss(request.body);

            if (Json::parseFromStream(builder, iss, &root, &errs)) {
                if (root.isMember("mode") && root["mode"].isString()) {
                    mode = root["mode"].asString();
                }
            }
        } catch (const std::exception& e) {
            response.statusCode = 400;
            response.contentType = "application/json";
            response.body = "{\"error\": \"Invalid JSON in request body\"}";
            return response;
        }
    }

    auto result = m_cameraController->setDriveMode(cameraId, mode);
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiSetFocusMode(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    std::string mode = "";

    // Parse JSON body to get focus mode
    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream iss(request.body);

            if (Json::parseFromStream(builder, iss, &root, &errs)) {
                if (root.isMember("mode") && root["mode"].isString()) {
                    mode = root["mode"].asString();
                }
            }
        } catch (const std::exception& e) {
            response.statusCode = 400;
            response.contentType = "application/json";
            response.body = "{\"error\": \"Invalid JSON in request body\"}";
            return response;
        }
    }

    auto result = m_cameraController->setFocusMode(cameraId, mode);
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiSetFocusArea(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    std::string area = "";

    // Parse JSON body to get focus area
    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream iss(request.body);

            if (Json::parseFromStream(builder, iss, &root, &errs)) {
                if (root.isMember("area") && root["area"].isString()) {
                    area = root["area"].asString();
                }
            }
        } catch (const std::exception& e) {
            response.statusCode = 400;
            response.contentType = "application/json";
            response.body = "{\"error\": \"Invalid JSON in request body\"}";
            return response;
        }
    }

    auto result = m_cameraController->setFocusArea(cameraId, area);
    response.contentType = "application/json";
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

// ==============================================================================
// NEW RESTful Generic Handlers - Scalable API Implementation
// ==============================================================================

HttpResponse CameraWebServer::handleApiConnectCamera(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "🔌 RESTful Connect: POST /api/cameras/" << cameraId << "/connection" << std::endl;

    // Parse JSON body for connection parameters (mode, username, password, reconnecting)
    std::string connectionMode = "remote-transfer";
    std::string username = "";
    std::string password = "";
    std::string reconnecting = "off"; // default to off for backward compatibility

    if (!request.body.empty()) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream bodyStream(request.body);

        if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
            if (root.isMember("mode")) {
                connectionMode = root["mode"].asString();
            }
            if (root.isMember("username")) {
                username = root["username"].asString();
            }
            if (root.isMember("password")) {
                password = root["password"].asString();
            }
            if (root.isMember("reconnecting")) {
                reconnecting = root["reconnecting"].asString();
            }
        }
    }

    auto result = m_cameraController->connectCamera(connectionMode, cameraId, username, password, reconnecting);
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiDisconnectCamera(const std::string& cameraId) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "🔌 RESTful Disconnect: DELETE /api/cameras/" << cameraId << "/connection" << std::endl;

    auto result = m_cameraController->disconnectCamera(cameraId);
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiGetConnectionStatus(const std::string& cameraId) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "🔌 RESTful Connection Status: GET /api/cameras/" << cameraId << "/connection" << std::endl;

    auto result = m_cameraController->getStatus(cameraId);
    response.body = m_cameraController->toJson(result);
    response.statusCode = 200;

    return response;
}

HttpResponse CameraWebServer::handleApiGetPropertyGeneric(const std::string& cameraId, const std::string& propertyName) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "📥 RESTful Get Property: GET /api/cameras/" << cameraId << "/properties/" << propertyName << std::endl;

    auto result = m_cameraController->getPropertyGeneric(cameraId, propertyName);
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiSetPropertyGeneric(const std::string& cameraId, const std::string& propertyName, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "📤 RESTful Set Property: PUT /api/cameras/" << cameraId << "/properties/" << propertyName << std::endl;

    // Parse JSON body for property value
    std::string value;
    if (!request.body.empty()) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream bodyStream(request.body);

        if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
            if (root.isMember("value")) {
                // Handle both string and numeric values
                if (root["value"].isString()) {
                    value = root["value"].asString();
                } else if (root["value"].isInt()) {
                    value = std::to_string(root["value"].asInt());
                } else if (root["value"].isUInt()) {
                    value = std::to_string(root["value"].asUInt());
                } else if (root["value"].isInt64()) {
                    value = std::to_string(root["value"].asInt64());
                }
            } else {
                response.statusCode = 400;
                response.body = R"({"success": false, "message": "Missing 'value' in request body"})";
                return response;
            }
        } else {
            response.statusCode = 400;
            response.body = R"({"success": false, "message": "Failed to parse JSON body"})";
            return response;
        }
    } else {
        response.statusCode = 400;
        response.body = R"({"success": false, "message": "Empty request body"})";
        return response;
    }

    auto result = m_cameraController->setPropertyGeneric(cameraId, propertyName, value);
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiExecuteActionGeneric(const std::string& cameraId, const std::string& actionName, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "🎬 RESTful Execute Action: POST /api/cameras/" << cameraId << "/actions/" << actionName << std::endl;

    // Parse JSON body for action parameters (if any)
    std::string params = "";
    if (!request.body.empty()) {
        // For zoom and shutter actions, pass the entire body to allow parsing parameters
        if (actionName == "zoom" || actionName == "shutter" || actionName == "focus-near-far") {
            params = request.body;
        } else {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream bodyStream(request.body);

            if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                if (root.isMember("params")) {
                    params = root["params"].asString();
                }
            }
        }
    }

    auto result = m_cameraController->executeActionGeneric(cameraId, actionName, params);
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiGetSaveDestination(const std::string& cameraId) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "📁 Get Save Destination: GET /api/cameras/" << cameraId << "/settings/save-destination" << std::endl;

    auto result = m_cameraController->getSaveDestination(cameraId);
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiSetSaveDestination(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "📁 Set Save Destination: PUT /api/cameras/" << cameraId << "/settings/save-destination" << std::endl;

    // Parse JSON body to extract path, prefix, and startNo
    std::string path = "";
    std::string prefix = "";
    int startNo = -1;

    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::istringstream bodyStream(request.body);
            std::string errs;

            if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                if (root.isMember("path")) {
                    path = root["path"].asString();
                }
                if (root.isMember("prefix")) {
                    prefix = root["prefix"].asString();
                }
                if (root.isMember("startNo")) {
                    startNo = root["startNo"].asInt();
                }
            } else {
                response.statusCode = 400;
                response.body = R"({"success": false, "message": "Failed to parse JSON body"})";
                return response;
            }
        } catch (const std::exception& e) {
            response.statusCode = 400;
            response.body = R"({"success": false, "message": "Invalid JSON body"})";
            return response;
        }
    } else {
        response.statusCode = 400;
        response.body = R"({"success": false, "message": "Empty request body"})";
        return response;
    }

    if (path.empty()) {
        response.statusCode = 400;
        response.body = R"({"success": false, "message": "Missing 'path' in request body"})";
        return response;
    }

    auto result = m_cameraController->setSaveDestination(cameraId, path, prefix, startNo);
    response.body = m_cameraController->toJson(result);
    response.statusCode = result.success ? 200 : 400;

    return response;
}

HttpResponse CameraWebServer::handleApiListSDCardFiles(const std::string& cameraId, const std::string& slotNumber) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "📂 List SD Card Files: GET /api/cameras/" << cameraId << "/sd-card/slot/" << slotNumber << "/files" << std::endl;

    // Get camera device
    auto cameraDevice = m_cameraController->getCameraDevice(cameraId);
    if (!cameraDevice) {
        response.statusCode = 404;
        response.body = R"({"success": false, "message": "Camera not found"})";
        return response;
    }

    // Dispatch based on connection mode
    int slot = std::stoi(slotNumber);
    auto connMode = m_cameraController->getConnectionMode(cameraId);

    if (connMode == cli::ConnectionMode::Remote) {
        response.statusCode = 400;
        response.body = R"({"success": false, "message": "SD card file listing not available in remote mode. Use contents-transfer or remote-transfer mode."})";
        return response;
    }

    Json::Value root;
    Json::Value filesArray(Json::arrayValue);

    if (connMode == cli::ConnectionMode::ContentsTransfer) {
        // Contents-transfer mode uses MTP-based getContentsList()
        auto result = cameraDevice->list_contents_transfer_files();
        if (!result.success) {
            response.statusCode = 400;
            root["success"] = false;
            root["message"] = result.error_message;
            response.body = root.toStyledString();
            return response;
        }

        root["success"] = true;
        root["slot"] = slot;
        root["file_count"] = static_cast<int>(result.files.size());

        for (const auto& file : result.files) {
            Json::Value fileObj;
            fileObj["content_id"] = file.handle;
            fileObj["file_id"] = 0;
            fileObj["file_path"] = file.fileName;
            fileObj["file_size"] = static_cast<Json::UInt64>(file.fileSize);
            fileObj["width"] = file.width;
            fileObj["height"] = file.height;

            // Parse date string "YYYYMMDDTHHMMSS"
            if (file.date.size() >= 15) {
                fileObj["creation_year"] = std::stoi(file.date.substr(0, 4));
                fileObj["creation_month"] = std::stoi(file.date.substr(4, 2));
                fileObj["creation_day"] = std::stoi(file.date.substr(6, 2));
                fileObj["creation_hour"] = std::stoi(file.date.substr(9, 2));
                fileObj["creation_minute"] = std::stoi(file.date.substr(11, 2));
                fileObj["creation_second"] = std::stoi(file.date.substr(13, 2));
            }

            filesArray.append(fileObj);
        }
    } else {
        // Remote-transfer mode uses GetRemoteTransferContentsInfoList SDK API
        auto result = cameraDevice->list_remote_transfer_contents(slot);
        if (!result.success) {
            response.statusCode = 400;
            root["success"] = false;
            root["message"] = result.error_message;
            response.body = root.toStyledString();
            return response;
        }

        root["success"] = true;
        root["slot"] = slot;
        root["file_count"] = static_cast<int>(result.contents.size());

        for (const auto& content : result.contents) {
            for (CrInt32u i = 0; i < content.filesNum; i++) {
                Json::Value fileObj;
                fileObj["content_id"] = content.contentId;
                fileObj["file_id"] = content.files[i].fileId;
                fileObj["file_path"] = content.files[i].filePath;
                fileObj["file_size"] = static_cast<Json::UInt64>(content.files[i].fileSize);

                fileObj["creation_year"] = content.creationDatetimeLocaltime.year;
                fileObj["creation_month"] = content.creationDatetimeLocaltime.month;
                fileObj["creation_day"] = content.creationDatetimeLocaltime.day;
                fileObj["creation_hour"] = content.creationDatetimeLocaltime.hour;
                fileObj["creation_minute"] = content.creationDatetimeLocaltime.minute;
                fileObj["creation_second"] = content.creationDatetimeLocaltime.sec;

                filesArray.append(fileObj);
            }
        }
    }

    root["files"] = filesArray;
    response.body = root.toStyledString();
    response.statusCode = 200;

    return response;
}

HttpResponse CameraWebServer::handleApiDownloadSDCardFile(
    const std::string& cameraId,
    const std::string& slotNumber,
    const std::string& contentId,
    const std::string& fileId,
    const HttpRequest& request)
{
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "⬇️  Download SD Card File: POST /api/cameras/" << cameraId
              << "/sd-card/slot/" << slotNumber << "/files/" << contentId << "/" << fileId << "/download" << std::endl;

    // Get camera device
    auto cameraDevice = m_cameraController->getCameraDevice(cameraId);
    if (!cameraDevice) {
        response.statusCode = 404;
        response.body = R"({"success": false, "message": "Camera not found"})";
        return response;
    }

    // Parse save path from request body (optional)
    std::string savePath = ""; // Empty = use connection-time SetSaveInfo path
    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::istringstream bodyStream(request.body);
            std::string errs;

            if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                if (root.isMember("save_path")) {
                    savePath = root["save_path"].asString();
                }
            }
        } catch (const std::exception& e) {
            // Ignore parsing errors, use default path
        }
    }

    // Download the file — dispatch based on connection mode
    int slot = std::stoi(slotNumber);
    CrInt32u content = std::stoul(contentId);
    CrInt32u file = std::stoul(fileId);

    auto connMode = m_cameraController->getConnectionMode(cameraId);
    CameraDevice::FileDownloadResult result;

    if (connMode == cli::ConnectionMode::ContentsTransfer) {
        // Contents-transfer uses MTP PullContentsFile with content handle
        result = cameraDevice->download_contents_transfer_file(content, savePath);
    } else {
        // Remote-transfer uses GetRemoteTransferContentsDataFile
        result = cameraDevice->download_remote_transfer_file(slot, content, file, savePath);
    }

    Json::Value root;
    root["success"] = result.success;

    if (result.success) {
        root["message"] = result.message.empty() ? "Download started" : result.message;
        response.statusCode = 202; // Accepted — download is async
    } else {
        std::string message = result.error_message;
        if (message.find("0x00008D02") != std::string::npos) {
            message = "Failed to start file download. Confirm the file identifiers are valid for the current connection mode and retry (SDK error 0x00008D02).";
        }
        root["message"] = message;
        response.statusCode = 400;
    }

    response.body = root.toStyledString();
    return response;
}

HttpResponse CameraWebServer::handleApiDownloadCompressed(
    const std::string& cameraId,
    const std::string& slotNumber,
    const std::string& contentId,
    const std::string& fileId,
    const std::string& type,
    const HttpRequest& request)
{
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "🖼️  Download " << type << ": POST /api/cameras/" << cameraId
              << "/sd-card/slot/" << slotNumber << "/files/" << contentId << "/" << fileId << "/" << type << std::endl;

    auto cameraDevice = m_cameraController->getCameraDevice(cameraId);
    if (!cameraDevice) {
        response.statusCode = 404;
        response.body = R"({"success": false, "message": "Camera not found"})";
        return response;
    }

    // Parse save path from request body (optional)
    std::string savePath = "";
    if (!request.body.empty()) {
        try {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::istringstream bodyStream(request.body);
            std::string errs;
            if (Json::parseFromStream(builder, bodyStream, &root, &errs)) {
                if (root.isMember("save_path")) {
                    savePath = root["save_path"].asString();
                }
            }
        } catch (const std::exception& e) {
            // Ignore parsing errors, use default path
        }
    }

    int slot = std::stoi(slotNumber);
    CrInt32u content = std::stoul(contentId);
    CrInt32u file = std::stoul(fileId);

    CameraDevice::FileDownloadResult result;
    if (type == "thumbnail") {
        result = cameraDevice->download_remote_transfer_thumbnail(slot, content, file, savePath);
    } else {
        result = cameraDevice->download_remote_transfer_screennail(slot, content, file, savePath);
    }

    Json::Value root;
    root["success"] = result.success;

    if (result.success) {
        root["message"] = result.message.empty() ? (type + " download started") : result.message;
        root["type"] = type;
        response.statusCode = 202;
    } else {
        root["message"] = result.error_message;
        response.statusCode = 400;
    }

    response.body = root.toStyledString();
    return response;
}

// ==============================================================================
// Camera Settings File Save/Load Handlers
// ==============================================================================

HttpResponse CameraWebServer::handleApiDownloadCameraSettings(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "💾 API: Download camera settings for camera " << cameraId << std::endl;

    // Parse JSON body for filename
    std::string filename = "CUMSET.DAT"; // default
    if (!request.body.empty()) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream bodyStream(request.body);
        if (Json::parseFromStream(builder, bodyStream, &root, &errors)) {
            if (root.isMember("filename") && root["filename"].isString()) {
                filename = root["filename"].asString();
            }
        }
    }

    // Call controller method
    ApiResponse apiResponse = m_cameraController->downloadCameraSettings(cameraId, filename);

    // Convert ApiResponse to HTTP response
    Json::Value jsonResponse;
    jsonResponse["success"] = apiResponse.success;
    jsonResponse["message"] = apiResponse.message;

    if (apiResponse.success) {
        jsonResponse["data"] = Json::Value(Json::objectValue);
        for (const auto& pair : apiResponse.data) {
            jsonResponse["data"][pair.first] = pair.second;
        }
        response.statusCode = 200;
    } else {
        response.statusCode = 400;
    }

    response.body = jsonResponse.toStyledString();
    return response;
}

HttpResponse CameraWebServer::handleApiUploadCameraSettings(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "📤 API: Upload camera settings for camera " << cameraId << std::endl;

    // Parse JSON body for filename
    std::string filename;
    if (!request.body.empty()) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream bodyStream(request.body);
        if (Json::parseFromStream(builder, bodyStream, &root, &errors)) {
            if (root.isMember("filename") && root["filename"].isString()) {
                filename = root["filename"].asString();
            }
        }
    }

    if (filename.empty()) {
        Json::Value jsonResponse;
        jsonResponse["success"] = false;
        jsonResponse["message"] = "Filename is required in request body";
        response.statusCode = 400;
        response.body = jsonResponse.toStyledString();
        return response;
    }

    // Call controller method
    ApiResponse apiResponse = m_cameraController->uploadCameraSettings(cameraId, filename);

    // Convert ApiResponse to HTTP response
    Json::Value jsonResponse;
    jsonResponse["success"] = apiResponse.success;
    jsonResponse["message"] = apiResponse.message;

    if (apiResponse.success) {
        jsonResponse["data"] = Json::Value(Json::objectValue);
        for (const auto& pair : apiResponse.data) {
            jsonResponse["data"][pair.first] = pair.second;
        }
        response.statusCode = 200;
    } else {
        response.statusCode = 400;
    }

    response.body = jsonResponse.toStyledString();
    return response;
}

HttpResponse CameraWebServer::handleApiListCameraSettings() {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "📋 API: List saved camera settings files" << std::endl;

    // Call controller method
    ApiResponse apiResponse = m_cameraController->listSavedSettings();

    // Convert ApiResponse to HTTP response
    Json::Value jsonResponse;
    jsonResponse["success"] = apiResponse.success;
    jsonResponse["message"] = apiResponse.message;

    if (apiResponse.success) {
        // Extract file list from data
        std::string countStr = apiResponse.data.count("count") ? apiResponse.data.at("count") : "0";
        int count = std::stoi(countStr);

        jsonResponse["files"] = Json::Value(Json::arrayValue);
        for (int i = 0; i < count; ++i) {
            std::string key = "file_" + std::to_string(i);
            if (apiResponse.data.count(key)) {
                jsonResponse["files"].append(apiResponse.data.at(key));
            }
        }
        jsonResponse["count"] = count;
        response.statusCode = 200;
    } else {
        response.statusCode = 400;
    }

    response.body = jsonResponse.toStyledString();
    return response;
}

HttpResponse CameraWebServer::handleApiImportLUT(const std::string& cameraId, const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";

    std::cout << "🎨 API: Import LUT file for camera " << cameraId << std::endl;

    // Parse JSON body for filePath and slot
    std::string filePath;
    int slot = 1;
    if (!request.body.empty()) {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream bodyStream(request.body);
        if (Json::parseFromStream(builder, bodyStream, &root, &errors)) {
            if (root.isMember("filePath") && root["filePath"].isString()) {
                filePath = root["filePath"].asString();
            }
            if (root.isMember("slot") && root["slot"].isInt()) {
                slot = root["slot"].asInt();
            }
        }
    }

    if (filePath.empty()) {
        Json::Value jsonResponse;
        jsonResponse["success"] = false;
        jsonResponse["message"] = "filePath is required in request body. Optionally provide slot (1-16, default: 1).";
        response.statusCode = 400;
        response.body = jsonResponse.toStyledString();
        return response;
    }

    ApiResponse apiResponse = m_cameraController->importLUT(cameraId, filePath, slot);

    Json::Value jsonResponse;
    jsonResponse["success"] = apiResponse.success;
    jsonResponse["message"] = apiResponse.message;
    if (apiResponse.success) {
        jsonResponse["data"]["slot"] = slot;
        jsonResponse["data"]["file"] = filePath;
        response.statusCode = 202; // Accepted — async operation
    } else {
        response.statusCode = 400;
    }

    response.body = jsonResponse.toStyledString();
    return response;
}

// ==============================================================================
// Server Management Endpoints
// ==============================================================================

void CameraWebServer::addLog(const std::string& level, const std::string& message, const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(m_logMutex);

    // Generate ISO timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&time_t_now));

    LogEntry entry;
    entry.timestamp = buf;
    entry.level = level;
    entry.message = message;
    entry.cameraId = cameraId;

    m_logBuffer.push_back(entry);
    if (m_logBuffer.size() > MAX_LOG_ENTRIES) {
        m_logBuffer.pop_front();
    }
}

HttpResponse CameraWebServer::handleApiServerStatus() {
    HttpResponse response;
    response.contentType = "application/json";
    response.statusCode = 200;

    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count();

    // Get camera counts from controller
    int connectedCount = 0;
    int discoveredCount = 0;
    if (m_cameraController) {
        auto cameras = m_cameraController->getAvailableCameras();
        discoveredCount = static_cast<int>(cameras.size());
        for (const auto& cam : cameras) {
            if (cam.connected) connectedCount++;
        }
    }

    // Detect platform
    std::string platform;
#if defined(__APPLE__)
    #if defined(__aarch64__)
        platform = "darwin-arm64";
    #else
        platform = "darwin-x64";
    #endif
#elif defined(__linux__)
    #if defined(__aarch64__)
        platform = "linux-arm64";
    #elif defined(__arm__)
        platform = "linux-arm";
    #else
        platform = "linux-x64";
    #endif
#elif defined(_WIN32)
    platform = "win32-x64";
#else
    platform = "unknown";
#endif

    std::ostringstream json;
    json << "{\n"
         << "  \"success\": true,\n"
         << "  \"server\": {\n"
         << "    \"version\": \"3.0.0\",\n"
         << "    \"sdkVersion\": \"V2.01.00\",\n"
         << "    \"uptime\": " << uptime << ",\n"
         << "    \"platform\": \"" << platform << "\"\n"
         << "  },\n"
         << "  \"cameras\": {\n"
         << "    \"connected\": " << connectedCount << ",\n"
         << "    \"discovered\": " << discoveredCount << "\n"
         << "  }\n"
         << "}";

    response.body = json.str();
    return response;
}

HttpResponse CameraWebServer::handleApiServerLogs(const HttpRequest& request) {
    HttpResponse response;
    response.contentType = "application/json";
    response.statusCode = 200;

    // Parse query parameters: ?lines=100&level=info
    int maxLines = 100;
    std::string minLevel = "info";

    // Query params could be parsed from request.path if needed in the future

    std::lock_guard<std::mutex> lock(m_logMutex);

    // Filter by level
    auto levelPriority = [](const std::string& level) -> int {
        if (level == "debug") return 0;
        if (level == "info") return 1;
        if (level == "warn") return 2;
        if (level == "error") return 3;
        return 1;
    };

    int minPriority = levelPriority(minLevel);

    std::ostringstream json;
    json << "{\n  \"success\": true,\n  \"logs\": [\n";

    int count = 0;
    int total = static_cast<int>(m_logBuffer.size());

    // Iterate from the end (most recent first)
    for (auto it = m_logBuffer.rbegin(); it != m_logBuffer.rend() && count < maxLines; ++it) {
        if (levelPriority(it->level) >= minPriority) {
            if (count > 0) json << ",\n";
            json << "    {"
                 << "\"timestamp\":\"" << it->timestamp << "\","
                 << "\"level\":\"" << it->level << "\","
                 << "\"message\":\"" << it->message << "\"";
            if (!it->cameraId.empty()) {
                json << ",\"cameraId\":\"" << it->cameraId << "\"";
            }
            json << "}";
            count++;
        }
    }

    json << "\n  ],\n  \"total\": " << total << "\n}";

    response.body = json.str();
    return response;
}

HttpResponse CameraWebServer::handleApiServerShutdown() {
    HttpResponse response;
    response.contentType = "application/json";
    response.statusCode = 200;
    response.body = R"({"success": true, "message": "Shutdown initiated"})";

    addLog("info", "Graceful shutdown requested via API");

    // Schedule shutdown on a separate thread so the response can be sent first.
    // We deliberately do NOT call std::exit() here — that bypasses stack
    // unwinding so the CameraWebServer/Controller destructors never run, and
    // the SDK + camera handle leak (camera stays in a half-connected state
    // until power-cycle). Setting m_running = false (via stop()) lets main()
    // fall out of its `while (server.isRunning())` loop and return cleanly,
    // which destroys the server, then the controller, then disconnects every
    // attached camera through ~CameraWebController.
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "\n[Shutdown] Graceful shutdown requested via /api/server/shutdown\n";
        this->stop();
    }).detach();

    return response;
}

} // namespace cli
