#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include "CameraWebServer.h"

// Global server instance for signal handling (atomic for safe concurrent access)
std::atomic<cli::CameraWebServer*> g_server{nullptr};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down web server..." << std::endl;
    auto* server = g_server.load();
    if (server) {
        server->stop();
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;

    // Parse --port argument
    for (int i = 1; i < argc; i++) {
        if ((std::strcmp(argv[i], "--port") == 0 || std::strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            port = std::atoi(argv[++i]);
            if (port < 1 || port > 65535) {
                std::cerr << "Invalid port: " << port << " (must be 1-65535)" << std::endl;
                return 1;
            }
        }
    }

    std::cout << "=== Camera Remote Control Web Server ===" << std::endl;
    std::cout << "Starting web-based camera control interface..." << std::endl;
    std::cout << std::endl;

    // Ignore SIGPIPE — prevents server crash when a client disconnects mid-response.
    // Without this, any send() call on a closed socket raises SIGPIPE and kills the process.
    // SIGPIPE does not exist on Windows (sockets use WSAECONNRESET instead).
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Create and start web server
    cli::CameraWebServer server(port);
    g_server.store(&server);

    if (!server.start()) {
        std::cerr << "Failed to start web server!" << std::endl;
        g_server.store(nullptr);
        return 1;
    }

    std::cout << std::endl;
    std::cout << "Web interface available at:" << std::endl;
    std::cout << "   Local:    http://localhost:" << port << std::endl;
    std::cout << "   Network:  http://<your-ip>:" << port << std::endl;
    std::cout << std::endl;
    std::cout << "📱 From your other PC, open the network URL in a web browser" << std::endl;
    std::cout << "⚡ API endpoints available:" << std::endl;
    std::cout << "   GET  /api/status           - Get camera status" << std::endl;
    std::cout << "   POST /api/camera/connect   - Connect to camera" << std::endl;
    std::cout << "   POST /api/camera/disconnect - Disconnect camera" << std::endl;
    std::cout << "   GET  /api/cameras          - List available cameras" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop the server..." << std::endl;
    std::cout << std::endl;
    
    // Keep server running until interrupted
    while (server.isRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Null out global pointer before server goes out of scope (prevents signal handler dangling pointer)
    g_server.store(nullptr);

    std::cout << "Web server has been stopped." << std::endl;
    return 0;
}