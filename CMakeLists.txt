cmake_minimum_required(VERSION 3.20)

# Default to RelWithDebInfo: -O2 with debug symbols
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

project(viewpoints LANGUAGES CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# macOS: build as app bundle with proper Info.plist
if(APPLE)
    set(CMAKE_MACOSX_BUNDLE TRUE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "Viewpoints")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.viewpoints.app")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "1")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "3.0.0")
    set(MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/Info.plist.in")
endif()

# --- wxWidgets ---
find_package(wxWidgets REQUIRED COMPONENTS core base)

# --- wgpu-native (WebGPU) ---
find_library(WGPU_LIBRARY NAMES wgpu_native REQUIRED)
find_path(WGPU_INCLUDE_DIR NAMES webgpu.h REQUIRED)

# --- Apache Arrow / Parquet ---
# Prefer Homebrew over Anaconda to avoid header/library version mismatch
list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
find_package(Arrow QUIET)
find_package(Parquet QUIET)

# --- Sources ---
set(VIEWPOINTS_SOURCES
    src/main.cpp
    src/ViewpointsApp.cpp
    src/MainFrame.cpp
    src/WebGPUCanvas.cpp
    src/DataManager.cpp
    src/ControlPanel.cpp
    src/WebGPUContext.cpp
    src/Normalize.cpp
    src/ColorMap.cpp
    $<$<PLATFORM_ID:Darwin>:src/ColorPanelHelper.mm>
)

# --- Executable ---
add_executable(vp ${VIEWPOINTS_SOURCES})

target_include_directories(vp PRIVATE
    ${WGPU_INCLUDE_DIR}
    ${wxWidgets_INCLUDE_DIRS}
    src
)
target_compile_definitions(vp PRIVATE ${wxWidgets_DEFINITIONS})

target_link_libraries(vp PRIVATE
    ${wxWidgets_LIBRARIES}
    ${WGPU_LIBRARY}
)

# Link Arrow/Parquet if found
if(Arrow_FOUND AND Parquet_FOUND)
    target_link_libraries(vp PRIVATE Arrow::arrow_shared Parquet::parquet_shared)
    target_compile_definitions(vp PRIVATE HAS_PARQUET=1)
    message(STATUS "Parquet support: enabled")
else()
    message(STATUS "Parquet support: disabled (install apache-arrow)")
endif()

# macOS frameworks needed by wgpu-native (Metal backend)
if(APPLE)
    target_link_libraries(vp PRIVATE
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework CoreGraphics"
    )
    # Compile Objective-C++ for macOS surface creation
    set_source_files_properties(src/WebGPUCanvas.cpp PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )
endif()
