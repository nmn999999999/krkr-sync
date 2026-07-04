cmake_minimum_required(VERSION 3.20)
project(krkr_sync VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detect platform
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(PLATFORM_IOS TRUE)
    add_definitions(-DPLATFORM_IOS=1)
elseif(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    add_definitions(-DPLATFORM_WINDOWS=1 -D_WINSOCK_DEPRECATED_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
else()
    set(PLATFORM_DESKTOP TRUE)
    add_definitions(-DPLATFORM_DESKTOP=1)
endif()

if(MSVC)
    add_compile_options(/W4 /MP /utf-8)
endif()

# Source files
set(COMMON_SOURCES
    src/core/protocol.cpp
    src/core/game_scanner.cpp
    src/core/sync_engine.cpp
    src/network/tcp_server.cpp
    src/network/tcp_client.cpp
    src/network/discovery.cpp
    src/ai/deepseek_api.cpp
)

if(PLATFORM_IOS)
    set(PLATFORM_SOURCES src/platform/platform_ios.mm)
else()
    set(PLATFORM_SOURCES src/platform/platform.cpp)
endif()

# Build static library
add_library(krkr_sync_lib STATIC ${COMMON_SOURCES} ${PLATFORM_SOURCES})
target_include_directories(krkr_sync_lib PUBLIC include)

if(PLATFORM_IOS)
    enable_language(OBJCXX)
    set_target_properties(krkr_sync_lib PROPERTIES
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    )
    target_link_libraries(krkr_sync_lib PUBLIC
        "-framework Foundation"
        "-framework Security"
    )
elseif(PLATFORM_WINDOWS)
    target_link_libraries(krkr_sync_lib PUBLIC ws2_32 winhttp crypt32)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(krkr_sync_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Build executables (Windows/macOS/Linux only)
if(NOT PLATFORM_IOS AND (WIN32 OR APPLE OR UNIX))
    add_executable(krkr_sync_server src/main_server.cpp)
    target_link_libraries(krkr_sync_server PRIVATE krkr_sync_lib)

    add_executable(krkr_sync_client src/main_client.cpp)
    target_link_libraries(krkr_sync_client PRIVATE krkr_sync_lib)
endif()
