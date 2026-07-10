# Shared Core Library CMakeLists.txt

cmake_minimum_required(VERSION 3.10)

# Define shared include directories
set(SDK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include)
set(CORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(OPENCV_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../opencv/include)
set(SDK_SETUP_DOC ${CMAKE_CURRENT_SOURCE_DIR}/../../docs/SDK_SETUP.md)
get_filename_component(SDK_INCLUDE_DIR "${SDK_INCLUDE_DIR}" ABSOLUTE)
get_filename_component(CORE_INCLUDE_DIR "${CORE_INCLUDE_DIR}" ABSOLUTE)
get_filename_component(OPENCV_INCLUDE_DIR "${OPENCV_INCLUDE_DIR}" ABSOLUTE)
get_filename_component(SDK_SETUP_DOC "${SDK_SETUP_DOC}" ABSOLUTE)

# Define shared library directories
set(SDK_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/lib)
set(OPENCV_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../opencv)
get_filename_component(SDK_LIB_DIR "${SDK_LIB_DIR}" ABSOLUTE)
get_filename_component(OPENCV_LIB_DIR "${OPENCV_LIB_DIR}" ABSOLUTE)

set(SDK_REQUIRED_HEADERS
    CameraRemote_SDK.h
    CrCommandData.h
    CrControlCode.h
    CrDefines.h
    CrDeviceProperty.h
    CrError.h
    CrImageDataBlock.h
    CrOperationCode.h
    CrTypes.h
    ICrCameraObjectInfo.h
    IDeviceCallback.h
)

set(SDK_MISSING_HEADERS "")
foreach(SDK_HEADER IN LISTS SDK_REQUIRED_HEADERS)
    if(NOT EXISTS "${SDK_INCLUDE_DIR}/${SDK_HEADER}")
        list(APPEND SDK_MISSING_HEADERS "${SDK_INCLUDE_DIR}/${SDK_HEADER}")
    endif()
endforeach()

if(SDK_MISSING_HEADERS)
    string(REPLACE ";" "\n  " SDK_MISSING_HEADERS_TEXT "${SDK_MISSING_HEADERS}")
    message(FATAL_ERROR
        "Sony Camera Remote SDK headers are missing.\n"
        "Expected placement: shared/sdk/include/*.h\n"
        "Missing files:\n  ${SDK_MISSING_HEADERS_TEXT}\n"
        "Download the SDK from Sony and follow ${SDK_SETUP_DOC}."
    )
endif()

# Core sources
# NOTE: CameraDevice.cpp (Sony's interactive sample device class) is intentionally
# NOT compiled. The REST server uses its own MIT device layer
# (api/server/src/device/CameraDeviceRest) instead, so the stock sample device
# implementation is dead code here. Excluding it also avoids the sample's
# Windows/MSVC wchar_t / std::filesystem issues (the old fork carried private
# patches for those; stock does not). CameraDevice.h is still required as a
# header by the stock helpers below and is fetched by extract-sdk.sh.
set(CORE_SOURCES
    PropertyValueTable.cpp
    ConnectionInfo.cpp
    Text.cpp
    MessageDefine.cpp
    OpenCVWrapper.cpp
    CrDebugString.cpp
)

set(CORE_HEADERS
    CameraDevice.h
    PropertyValueTable.h
    ConnectionInfo.h
    Text.h
    MessageDefine.h
    OpenCVWrapper.h
    CrDebugString.h
)

# The wrapper sources are Sony sample-derived and are not tracked in this
# public repository. They are bundled in the Camera Remote SDK download and
# placed here by scripts/extract-sdk.sh. Fail early with a clear pointer if a
# contributor tries to build before running the extraction helper.
set(CORE_MISSING_SOURCES "")
foreach(CORE_FILE IN LISTS CORE_SOURCES CORE_HEADERS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${CORE_FILE}")
        list(APPEND CORE_MISSING_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${CORE_FILE}")
    endif()
endforeach()

if(CORE_MISSING_SOURCES)
    string(REPLACE ";" "\n  " CORE_MISSING_SOURCES_TEXT "${CORE_MISSING_SOURCES}")
    message(FATAL_ERROR
        "Sony Camera Remote SDK integration wrapper sources are missing.\n"
        "Expected placement: shared/core/*.cpp and shared/core/*.h\n"
        "Missing files:\n  ${CORE_MISSING_SOURCES_TEXT}\n"
        "These files ship inside the Camera Remote SDK download and are placed\n"
        "by scripts/extract-sdk.sh. Follow ${SDK_SETUP_DOC}."
    )
endif()

# Create shared core library
add_library(CameraRemoteCore STATIC ${CORE_SOURCES} ${CORE_HEADERS})

# Set C++ standard for shared library
set_target_properties(CameraRemoteCore PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

# Include directories for shared library
target_include_directories(CameraRemoteCore
    PUBLIC
        ${SDK_INCLUDE_DIR}
        ${CORE_INCLUDE_DIR}
        ${OPENCV_INCLUDE_DIR}
)

# Platform-specific settings
if(UNIX AND NOT APPLE)
    target_compile_options(CameraRemoteCore
        PUBLIC
            -fstack-protector-all
        PRIVATE
            -fsigned-char
    )

    # Handle GCC version compatibility
    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8)
            target_compile_definitions(CameraRemoteCore PRIVATE USE_EXPERIMENTAL_FS)
        endif()
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
            target_link_libraries(CameraRemoteCore PRIVATE stdc++fs)
        endif()
    endif()
endif()

# Find and link Camera Remote SDK
find_library(CAMERA_REMOTE_SDK Cr_Core HINTS ${SDK_LIB_DIR} NO_DEFAULT_PATH)
if(CAMERA_REMOTE_SDK)
    target_link_libraries(CameraRemoteCore PUBLIC ${CAMERA_REMOTE_SDK})
else()
    if(APPLE)
        set(SDK_EXPECTED_CORE_LIB "${SDK_LIB_DIR}/libCr_Core.dylib")
    elseif(WIN32)
        set(SDK_EXPECTED_CORE_LIB "${SDK_LIB_DIR}/Cr_Core.lib")
    else()
        set(SDK_EXPECTED_CORE_LIB "${SDK_LIB_DIR}/libCr_Core.so")
    endif()
    message(FATAL_ERROR
        "Sony Camera Remote SDK core library not found.\n"
        "Expected placement: ${SDK_EXPECTED_CORE_LIB}\n"
        "Download the SDK from Sony and follow ${SDK_SETUP_DOC}."
    )
endif()

# Platform-specific OpenCV linking
if(APPLE)
    set(OPENCV_LIBS
        ${OPENCV_LIB_DIR}/Darwin/Release/macos/bin/libopencv_core.408.dylib
        ${OPENCV_LIB_DIR}/Darwin/Release/macos/bin/libopencv_highgui.408.dylib
        ${OPENCV_LIB_DIR}/Darwin/Release/macos/bin/libopencv_imgcodecs.408.dylib
        ${OPENCV_LIB_DIR}/Darwin/Release/macos/bin/libopencv_imgproc.408.dylib
    )
elseif(WIN32)
    # Windows OpenCV linking will be handled in individual projects
    set(OPENCV_LIBS "")
else()
    # Linux OpenCV linking
    set(OPENCV_LIBS
        ${OPENCV_LIB_DIR}/Linux/libopencv_core.so.408
        ${OPENCV_LIB_DIR}/Linux/libopencv_highgui.so.408
        ${OPENCV_LIB_DIR}/Linux/libopencv_imgcodecs.so.408
        ${OPENCV_LIB_DIR}/Linux/libopencv_imgproc.so.408
    )
endif()

if(OPENCV_LIBS)
    target_link_libraries(CameraRemoteCore PUBLIC ${OPENCV_LIBS})
endif()

# Windows-specific settings
if(WIN32)
    # V2.01 SDK: no UNICODE needed — our code uses std::string throughout
endif()
