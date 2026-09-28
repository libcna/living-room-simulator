# =============================================================================
# living-room-simulator -- a realistic, explorable 3D living room rendered with CNA.
# =============================================================================
cmake_minimum_required(VERSION 3.23)

project(CnaRoom
    VERSION 0.1.0
    DESCRIPTION "A realistic 3D living room built on the CNA framework (EasyGL renderer)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The renderer CNA compiles in. OPENGLES3 is EasyGL's reference identity: every
# shader path is exercised there first. OPENGL33 shares the same EasyGL
# implementation on a desktop GL 3.3 core context.
if(EMSCRIPTEN)
    set(_cna_room_default_renderer "WEBGL2")
else()
    set(_cna_room_default_renderer "OPENGLES3")
endif()
set(CNA_ROOM_RENDERER "${_cna_room_default_renderer}" CACHE STRING
    "CNA graphics renderer to build against (WEBGL2 for Emscripten, OPENGLES3 or OPENGL33 natively)")

option(CNA_ROOM_BUILD_TESTS "Build living-room-simulator's unit tests" ON)

include(CnaRoomWarnings)
include(CnaRoomDependencies)

message(STATUS "living-room-simulator: ${PROJECT_VERSION}, renderer ${CNA_ROOM_RENDERER}, "
               "${CMAKE_BUILD_TYPE}, ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "living-room-simulator: CNA at ${CNA_ROOT_DIR}")

add_subdirectory("${CNA_ROOT_DIR}" cna EXCLUDE_FROM_ALL)

# --- the application ---------------------------------------------------------
# Everything except main() lives in a static library so the unit tests can link
# the same code the application runs.
file(GLOB_RECURSE CNA_ROOM_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM CNA_ROOM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")

add_library(cna_room_core STATIC ${CNA_ROOM_SOURCES})
add_library(CnaRoom::Core ALIAS cna_room_core)
target_include_directories(cna_room_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(Threads REQUIRED)
target_link_libraries(cna_room_core PUBLIC CNA PRIVATE CnaRoom::Warnings Threads::Threads)

if(EMSCRIPTEN)
    set(_cna_room_default_asset_dir "/assets")
else()
    set(_cna_room_default_asset_dir "${CMAKE_SOURCE_DIR}/assets")
endif()
set(CNA_ROOM_ASSET_DIR "${_cna_room_default_asset_dir}" CACHE PATH
    "Directory the application loads its assets from")
target_compile_definitions(cna_room_core PUBLIC
    CNA_ROOM_VERSION="${PROJECT_VERSION}"
    CNA_ROOM_RENDERER_NAME="${CNA_ROOM_RENDERER}"
    CNA_ROOM_DEFAULT_ASSET_DIR="${CNA_ROOM_ASSET_DIR}")

add_executable(living-room-simulator src/Main.cpp)
target_link_libraries(living-room-simulator PRIVATE CnaRoom::Core CnaRoom::Warnings)
if(EMSCRIPTEN)
    set_target_properties(living-room-simulator PROPERTIES SUFFIX ".html")
    target_link_libraries(living-room-simulator PRIVATE CNA::EmscriptenAsyncify)
    target_link_options(living-room-simulator PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        -sFORCE_FILESYSTEM=1
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        --shell-file "${CMAKE_SOURCE_DIR}/web/shell.html")

    # Ship the actual imported furniture used by RoomScene, rather than every
    # extracted source model. The files are fetched/extracted by the existing
    # asset pipeline and remain outside version control for license reasons.
    file(STRINGS "${CMAKE_SOURCE_DIR}/web/model-assets.txt" _web_models)
    foreach(_model IN LISTS _web_models)
        if(_model STREQUAL "" OR _model MATCHES "^#")
            continue()
        endif()
        set(_model_file "${CMAKE_SOURCE_DIR}/assets/external/extracted/${_model}.glb")
        if(NOT EXISTS "${_model_file}")
            message(FATAL_ERROR
                "Web build needs ${_model_file}. Run scripts/fetch-assets.sh and scripts/extract-assets.sh first.")
        endif()
        target_link_options(living-room-simulator PRIVATE
            "SHELL:--preload-file ${_model_file}@/assets/external/extracted/${_model}.glb")
    endforeach()
endif()
if(TARGET SDL3::SDL3main)
    target_link_libraries(living-room-simulator PRIVATE SDL3::SDL3main)
endif()

if(CNA_ROOM_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
