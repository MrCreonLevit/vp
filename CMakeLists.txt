cmake_minimum_required(VERSION 3.20)
project(viewpoints LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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
include(${wxWidgets_USE_FILE})

# --- wgpu-native (WebGPU) ---
find_library(WGPU_LIBRARY NAMES wgpu_native REQUIRED)
find_path(WGPU_INCLUDE_DIR NAMES webgpu.h REQUIRED)

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
)

# --- Executable ---
add_executable(viewpoints ${VIEWPOINTS_SOURCES})

target_include_directories(viewpoints PRIVATE
    ${WGPU_INCLUDE_DIR}
    src
)

target_link_libraries(viewpoints PRIVATE
    ${wxWidgets_LIBRARIES}
    ${WGPU_LIBRARY}
)

# macOS frameworks needed by wgpu-native (Metal backend)
if(APPLE)
    target_link_libraries(viewpoints PRIVATE
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
