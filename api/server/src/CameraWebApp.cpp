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

// Set by signalHandler, consumed by main()'s run loop.
std::atomic<int> g_signalReceived{0};

// Async-signal-safe: records the signal and returns. It must NOT call stop()
// directly — stop() does stream I/O, joins threads, and takes several mutexes,
// none of which are legal in a signal handler. Doing so also risks a hard
// deadlock: a signal delivered to a thread already holding the shutdown mutex
// would block the handler forever and make Ctrl-C stop working. main() does the
// actual teardown once it observes this flag.
void signalHandler(int signal) {
    g_signalReceived.store(signal);
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
    
    // Keep server running until interrupted, or until something else (the
    // detached POST /api/server/shutdown thread) clears the running flag.
    while (server.isRunning() && g_signalReceived.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (const int sig = g_signalReceived.load()) {
        std::cout << "\nReceived signal " << sig << ", shutting down web server..." << std::endl;
    }

    // Tear down explicitly rather than leaving it to ~CameraWebServer(). stop()
    // is idempotent and serialized, so this is safe even when the detached
    // shutdown thread is already inside it — we simply block until it is done.
    // Doing it here (not in the destructor) keeps the join on a thread that is
    // still fully constructed.
    server.stop();

    // Null out global pointer before server goes out of scope (prevents signal handler dangling pointer)
    g_server.store(nullptr);

    std::cout << "Web server has been stopped." << std::endl;
    return 0;
}