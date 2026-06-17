cmake_minimum_required(VERSION 3.19)

# Standalone build: invoked directly as the source root
# Integrated build: invoked via add_server_component from Server/CMakeLists.txt
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # ── STANDALONE BUILD (no Conan, no full server) ───────────────────────────

    project(omp-nui VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    get_filename_component(OMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

    # SDK sub-libraries (header-only)
    add_subdirectory("${OMP_ROOT}/SDK/lib/glm"                "${CMAKE_BINARY_DIR}/glm"              EXCLUDE_FROM_ALL)
    add_subdirectory("${OMP_ROOT}/SDK/lib/robin-hood-hashing"  "${CMAKE_BINARY_DIR}/robin-hood"       EXCLUDE_FROM_ALL)
    add_subdirectory("${OMP_ROOT}/SDK/lib/span-lite"           "${CMAKE_BINARY_DIR}/span-lite"        EXCLUDE_FROM_ALL)
    add_subdirectory("${OMP_ROOT}/SDK/lib/string-view-lite"    "${CMAKE_BINARY_DIR}/string-view-lite" EXCLUDE_FROM_ALL)

    add_library(OMP-SDK INTERFACE)
    target_link_libraries(OMP-SDK INTERFACE glm::glm robin_hood span-lite string-view-lite)
    target_compile_definitions(OMP-SDK INTERFACE
        GLM_FORCE_QUAT_DATA_WXYZ NOMINMAX GLM_FORCE_SSE2
        nssv_CONFIG_SELECT_STRING_VIEW=nssv_STRING_VIEW_NONSTD
        span_CONFIG_SELECT_SPAN=span_SPAN_NONSTD
    )
    if(MSVC)
        target_compile_options(OMP-SDK INTERFACE /arch:SSE2)
    else()
        target_compile_options(OMP-SDK INTERFACE -msse2 -mfpmath=sse)
    endif()
    target_include_directories(OMP-SDK INTERFACE "${OMP_ROOT}/SDK/include")

    add_library(OMP-Network STATIC
        "${OMP_ROOT}/Shared/Network/bitstream.cpp"
        "${OMP_ROOT}/Shared/Network/Encoding/str_compress.cpp"
        "${OMP_ROOT}/Shared/Network/Encoding/huffman_tree.cpp"
    )
    target_link_libraries(OMP-Network PUBLIC OMP-SDK)
    target_include_directories(OMP-Network PUBLIC "${OMP_ROOT}/Shared/Network")
    set_property(TARGET OMP-Network PROPERTY POSITION_INDEPENDENT_CODE ON)

    add_library(OMP-NetCode INTERFACE)
    target_link_libraries(OMP-NetCode INTERFACE OMP-Network)
    target_include_directories(OMP-NetCode INTERFACE "${OMP_ROOT}/Shared/NetCode")

    add_library(NUI SHARED nui_component.cpp)
    target_link_libraries(NUI PRIVATE OMP-SDK OMP-NetCode)
    target_include_directories(NUI PRIVATE
        "${OMP_ROOT}/lib/cpp-httplib"
        "${OMP_ROOT}/lib/pawn-natives"
        "${OMP_ROOT}/lib/pawn/source"
        "${OMP_ROOT}/Shared"
    )
    target_compile_definitions(NUI PRIVATE
        OMP_VERSION_MAJOR=1 OMP_VERSION_MINOR=5 OMP_VERSION_PATCH=8
        CPPHTTPLIB_NO_EXCEPTIONS
    )
    if(UNIX)
        target_link_libraries(NUI PRIVATE pthread)
        target_compile_options(NUI PRIVATE -fvisibility=hidden)
    endif()
    if(WIN32)
        target_link_libraries(NUI PRIVATE ws2_32)
        target_compile_definitions(NUI PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
    endif()
    if(MSVC)
        # Use dynamic CRT (matches open.mp server)
        set_property(TARGET NUI PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        set_property(TARGET OMP-Network PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        # Disable warnings-as-errors for httplib
        target_compile_options(NUI PRIVATE /W3 /wd4996 /wd4244)
    endif()
    set_property(TARGET NUI PROPERTY OUTPUT_NAME "NUI")
    set_property(TARGET NUI PROPERTY PREFIX "")

else()
    # ── INTEGRATED BUILD (invoked from Server/Components/CMakeLists.txt) ──────

    get_filename_component(ProjectId ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    add_server_component(${ProjectId})

    target_include_directories(${ProjectId} PRIVATE
        ${CMAKE_SOURCE_DIR}/lib/cpp-httplib
        ${CMAKE_SOURCE_DIR}/lib/pawn-natives
        ${CMAKE_SOURCE_DIR}/lib/pawn/source
        ${CMAKE_SOURCE_DIR}/Shared
    )
    target_compile_definitions(${ProjectId} PRIVATE CPPHTTPLIB_NO_EXCEPTIONS)

    if(UNIX)
        target_link_libraries(${ProjectId} PRIVATE pthread)
    endif()
    if(WIN32)
        target_link_libraries(${ProjectId} PRIVATE ws2_32)
    endif()
endif()
