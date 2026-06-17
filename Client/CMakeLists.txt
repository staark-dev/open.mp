cmake_minimum_required(VERSION 3.20)
project(omp-nui-client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Must build as 32-bit (Win32). Run: cmake -A Win32")
endif()

# ── CEF ──────────────────────────────────────────────────────────────────────
if(NOT DEFINED CEF_ROOT)
    set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/cef")
endif()

if(NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
    message(FATAL_ERROR
        "CEF not found at ${CEF_ROOT}\n"
        "Extract CEF Standard Distribution to Client/cef/")
endif()

# Build libcef_dll_wrapper from source (shipped in Standard Distribution)
file(GLOB_RECURSE CEF_WRAPPER_SRC "${CEF_ROOT}/libcef_dll/*.cc")
add_library(libcef_dll_wrapper STATIC ${CEF_WRAPPER_SRC})
target_include_directories(libcef_dll_wrapper PUBLIC
    "${CEF_ROOT}"
    "${CEF_ROOT}/include"
)
target_compile_definitions(libcef_dll_wrapper PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WRAPPING_CEF_SHARED
)

# ── CEF Helper Subprocess ────────────────────────────────────────────────────
# CEF spawns renderer/gpu/utility processes — they run this helper exe
add_executable(omp-nui-helper WIN32 helper/helper_main.cpp)
target_include_directories(omp-nui-helper PRIVATE "${CEF_ROOT}")
target_link_libraries(omp-nui-helper PRIVATE
    libcef_dll_wrapper
    "${CEF_ROOT}/Release/libcef.lib"
)
target_compile_definitions(omp-nui-helper PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)

# ── Main injected DLL ────────────────────────────────────────────────────────
add_library(omp-nui-client SHARED
    src/dllmain.cpp
    src/dx9_hook.cpp
    src/cef_manager.cpp
    src/rpc_receiver.cpp
)

target_include_directories(omp-nui-client PRIVATE
    "${CEF_ROOT}"
    "${CEF_ROOT}/include"
    src
)

target_link_libraries(omp-nui-client PRIVATE
    libcef_dll_wrapper
    "${CEF_ROOT}/Release/libcef.lib"
    d3d9
    ws2_32
    winmm
)

target_compile_definitions(omp-nui-client PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    UNICODE
    _UNICODE
)

set_target_properties(omp-nui-client PROPERTIES OUTPUT_NAME "omp-nui")

# ── Post-build: copy CEF runtime DLLs next to omp-nui.dll ───────────────────
add_custom_command(TARGET omp-nui-client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CEF_ROOT}/Release/libcef.dll"
        "${CEF_ROOT}/Release/chrome_elf.dll"
        "${CEF_ROOT}/Release/d3dcompiler_47.dll"
        "${CEF_ROOT}/Release/libEGL.dll"
        "${CEF_ROOT}/Release/libGLESv2.dll"
        "${CEF_ROOT}/Release/v8_context_snapshot.bin"
        "$<TARGET_FILE_DIR:omp-nui-client>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CEF_ROOT}/Resources"
        "$<TARGET_FILE_DIR:omp-nui-client>/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:omp-nui-helper>"
        "$<TARGET_FILE_DIR:omp-nui-client>/omp-nui-helper.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:omp-nui-client>/nui-local"
    COMMENT "Copying CEF runtime files and local NUI resources..."
)
