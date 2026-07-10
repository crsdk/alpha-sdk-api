# Web Application CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(CameraWebApp)

# Import shared variables from parent
set(SDK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/sdk/include)
set(CORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/core)
set(SDK_LIB_DIR_LOCAL ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/sdk/lib)
set(SDK_SETUP_DOC ${CMAKE_CURRENT_SOURCE_DIR}/../../docs/SDK_SETUP.md)
get_filename_component(SDK_INCLUDE_DIR "${SDK_INCLUDE_DIR}" ABSOLUTE)
get_filename_component(CORE_INCLUDE_DIR "${CORE_INCLUDE_DIR}" ABSOLUTE)
get_filename_component(SDK_LIB_DIR_LOCAL "${SDK_LIB_DIR_LOCAL}" ABSOLUTE)
get_filename_component(SDK_SETUP_DOC "${SDK_SETUP_DOC}" ABSOLUTE)

# Project name
set(WEBAPP_TARGET "CameraWebApp")

# Add shared core library from parent directory
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../shared/core ${CMAKE_CURRENT_BINARY_DIR}/core)

# Web app specific sources
set(WEBAPP_SOURCES
    src/CameraWebApp.cpp
    src/CameraWebController.cpp
    src/CameraWebServer.cpp
    src/device/CameraDeviceRest.cpp
    src/device/RestPropertyParsers.cpp
    third_party/jsoncpp/jsoncpp.cpp
)

set(WEBAPP_HEADERS
    src/CameraWebController.h
    src/CameraWebServer.h
    src/device/CameraDeviceRest.h
    src/device/RestPropertyParsers.h
)

# Create web app executable
add_executable(${WEBAPP_TARGET} ${WEBAPP_SOURCES} ${WEBAPP_HEADERS})

# JSON is provided by the vendored amalgamated jsoncpp (third_party/jsoncpp),
# compiled directly into the binary — no external jsoncpp package. OpenSSL is no
# longer a dependency: the WebSocket accept-key handshake uses a small
# self-contained SHA-1/Base64 in CameraWebServer.cpp.

# Add include directories
target_include_directories(${WEBAPP_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/jsoncpp
    ${SDK_INCLUDE_DIR}
    ${CORE_INCLUDE_DIR}
)

# On Linux, link Cr_Core directly to the executable (not just via static CameraRemoteCore)
# The static archive doesn't propagate shared library symbol resolution on Linux
if(UNIX AND NOT APPLE)
    find_library(CR_CORE_DIRECT Cr_Core HINTS ${SDK_LIB_DIR_LOCAL} NO_DEFAULT_PATH)
    if(NOT CR_CORE_DIRECT)
        message(FATAL_ERROR
            "Sony Camera Remote SDK core library not found.\n"
            "Expected placement: ${SDK_LIB_DIR_LOCAL}/libCr_Core.so\n"
            "Download the SDK from Sony and follow ${SDK_SETUP_DOC}."
        )
    endif()
endif()

# Link with shared core library (JSON is compiled in from third_party/jsoncpp).
# On Linux, Cr_Core must come AFTER CameraRemoteCore to resolve symbols
target_link_libraries(${WEBAPP_TARGET}
    PRIVATE
        CameraRemoteCore
        $<$<PLATFORM_ID:Linux>:${CR_CORE_DIRECT}>
)

# Set target properties
set_target_properties(${WEBAPP_TARGET} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# Platform-specific settings
if(APPLE)
    set_target_properties(${WEBAPP_TARGET} PROPERTIES
        BUILD_RPATH "@executable_path"
    )
elseif(UNIX)
    set_target_properties(${WEBAPP_TARGET} PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

# Platform-specific compile options
if(UNIX AND NOT APPLE)
    target_compile_options(${WEBAPP_TARGET}
        PUBLIC
            -fstack-protector-all
        PRIVATE
            -fsigned-char
    )
endif()

if(MSVC)
    # Static CRT for self-contained distribution (no VC++ Redistributable needed)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${WEBAPP_TARGET}
        PUBLIC
            /guard:cf
    )

    # Windows-specific OpenCV libraries
    target_link_libraries(${WEBAPP_TARGET} PRIVATE
        $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Release/lib/opencv_core480.lib>
        $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Release/lib/opencv_highgui480.lib>
        $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Release/lib/opencv_imgcodecs480.lib>
        $<$<CONFIG:Release>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Release/lib/opencv_imgproc480.lib>
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Debug/lib/opencv_core480d.lib>
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Debug/lib/opencv_highgui480d.lib>
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Debug/lib/opencv_imgcodecs480d.lib>
        $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64/Debug/lib/opencv_imgproc480d.lib>
    )
endif()

# Copy required library binaries for runtime
if(WIN32)
    # Windows library copying
    set(cr_ldir ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/sdk/lib)
    add_custom_command(TARGET ${WEBAPP_TARGET} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${cr_ldir} $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
    )

    # OpenCV DLLs for Windows — use generator expressions for multi-config generators (Visual Studio)
    set(OPENCV_WIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Windows/x86_64")
    add_custom_command(TARGET ${WEBAPP_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${OPENCV_WIN_DIR}/Debug/bin/opencv_core480d.dll,${OPENCV_WIN_DIR}/Release/bin/opencv_core480.dll>"
            $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${OPENCV_WIN_DIR}/Debug/bin/opencv_highgui480d.dll,${OPENCV_WIN_DIR}/Release/bin/opencv_highgui480.dll>"
            $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${OPENCV_WIN_DIR}/Debug/bin/opencv_imgcodecs480d.dll,${OPENCV_WIN_DIR}/Release/bin/opencv_imgcodecs480.dll>"
            $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${OPENCV_WIN_DIR}/Debug/bin/opencv_imgproc480d.dll,${OPENCV_WIN_DIR}/Release/bin/opencv_imgproc480.dll>"
            $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
    )
endif()

if(UNIX AND NOT APPLE)
    # Linux library copying
    set(cr_ldir ${CMAKE_CURRENT_SOURCE_DIR}/../../shared/sdk/lib)
    add_custom_command(TARGET ${WEBAPP_TARGET} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${cr_ldir} $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
    )

    add_custom_command(TARGET ${WEBAPP_TARGET} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Linux/libopencv_core.so.408" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Linux/libopencv_highgui.so.408" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Linux/libopencv_imgcodecs.so.408" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Linux/libopencv_imgproc.so.408" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
    )
endif()

if(APPLE)
    # macOS library copying
    set(cr_ldir ${SDK_LIB_DIR_LOCAL})
    set(cr_adapter_dir ${cr_ldir}/CrAdapter)
    set(ct_fw "Contents/Frameworks")
    set(adaptor_dir_name "CrAdapter")

    set(MACOS_SDK_RUNTIME_LIBS
        ${cr_ldir}/libCr_Core.dylib
        ${cr_ldir}/libmonitor_protocol.dylib
        ${cr_ldir}/libmonitor_protocol_pf.dylib
        ${cr_adapter_dir}/libCr_PTP_IP.dylib
        ${cr_adapter_dir}/libCr_PTP_USB.dylib
        ${cr_adapter_dir}/libssh2.dylib
        ${cr_adapter_dir}/libusb-1.0.0.dylib
    )

    set(MACOS_MISSING_SDK_RUNTIME_LIBS "")
    foreach(MACOS_SDK_RUNTIME_LIB IN LISTS MACOS_SDK_RUNTIME_LIBS)
        if(NOT EXISTS "${MACOS_SDK_RUNTIME_LIB}")
            list(APPEND MACOS_MISSING_SDK_RUNTIME_LIBS "${MACOS_SDK_RUNTIME_LIB}")
        endif()
    endforeach()

    if(MACOS_MISSING_SDK_RUNTIME_LIBS)
        string(REPLACE ";" "\n  " MACOS_MISSING_SDK_RUNTIME_TEXT "${MACOS_MISSING_SDK_RUNTIME_LIBS}")
        message(FATAL_ERROR
            "Sony Camera Remote SDK macOS runtime libraries are missing.\n"
            "Expected placement: shared/sdk/lib/*.dylib and shared/sdk/lib/CrAdapter/*.dylib\n"
            "Missing files:\n  ${MACOS_MISSING_SDK_RUNTIME_TEXT}\n"
            "Download the SDK from Sony and follow ${SDK_SETUP_DOC}."
        )
    endif()

    add_custom_command(TARGET ${WEBAPP_TARGET} PRE_BUILD
        # Create the CrAdapter directory structure the SDK expects at runtime
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${WEBAPP_TARGET}>/${ct_fw}/${adaptor_dir_name}
        # Copy core SDK libraries next to the binary
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cr_ldir}/libCr_Core.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cr_ldir}/libmonitor_protocol.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cr_ldir}/libmonitor_protocol_pf.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        # Copy adapter libraries into Contents/Frameworks/CrAdapter/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cr_adapter_dir}/libCr_PTP_IP.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>/${ct_fw}/${adaptor_dir_name}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cr_adapter_dir}/libCr_PTP_USB.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>/${ct_fw}/${adaptor_dir_name}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cr_adapter_dir}/libssh2.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>/${ct_fw}/${adaptor_dir_name}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${cr_adapter_dir}/libusb-1.0.0.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>/${ct_fw}/${adaptor_dir_name}/
        # Copy OpenCV libraries
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Darwin/Release/macos/bin/libopencv_core.408.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Darwin/Release/macos/bin/libopencv_highgui.408.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Darwin/Release/macos/bin/libopencv_imgcodecs.408.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/opencv/Darwin/Release/macos/bin/libopencv_imgproc.408.dylib" $<TARGET_FILE_DIR:${WEBAPP_TARGET}>
    )

    # No dylib relocation step is needed. The binary depends only on the Sony SDK
    # and OpenCV dylibs (copied next to it above and referenced via @rpath /
    # @executable_path) plus macOS system libraries. JSON is compiled in from
    # third_party/jsoncpp and the WebSocket handshake uses a self-contained
    # SHA-1/Base64, so there are no absolute Homebrew paths to rewrite.
endif()

# Install target
install(TARGETS ${WEBAPP_TARGET} DESTINATION bin)
