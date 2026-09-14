# ---------------------------------------------------------------------------
# Locating and configuring the upstream checkouts cna-room builds against.
# ---------------------------------------------------------------------------
# CNA installs and exports no CMake package, so the framework is consumed with
# add_subdirectory() from a sibling checkout. CNA in turn resolves
# sharp-runtime, easy-gl and meta-gl relative to *its own* source root, so the
# whole set lives side by side:
#
#   ../cna            branch next
#   ../sharp-runtime  branch next
#   ../easy-gl        branch develop   (EasyGL renderer family)
#   ../meta-gl        branch develop   (easy-gl's own dependency)
#
# scripts/fetch-dependencies.sh clones exactly that layout.
include_guard(GLOBAL)

set(CNA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cna" CACHE PATH
    "Path to the CNA framework checkout (a sibling of this repository by default)")

function(cna_room_require_checkout root name clone_url branch)
    if(EXISTS "${root}/CMakeLists.txt")
        return()
    endif()
    message(FATAL_ERROR
        "cna-room: ${name} not found at '${root}'.\n"
        "It is a separate repository, not part of this project. Fetch every\n"
        "dependency at once with\n"
        "    scripts/fetch-dependencies.sh\n"
        "or clone it yourself with\n"
        "    git clone --branch ${branch} ${clone_url} ${root}\n"
        "and pass -DCNA_ROOT_DIR=/path/to/cna if it does not sit beside this\n"
        "repository. README.md lists the full set.")
endfunction()

cna_room_require_checkout("${CNA_ROOT_DIR}" "the CNA framework"
    "https://github.com/libcna/cna.git" "next")
cna_room_require_checkout("${CNA_ROOT_DIR}/../sharp-runtime" "sharp-runtime"
    "https://github.com/libcna/sharp-runtime.git" "next")
if(CNA_ROOM_RENDERER MATCHES "^(OPENGL33|OPENGLES3|OPENGLES2)$")
    cna_room_require_checkout("${CNA_ROOT_DIR}/../easy-gl" "easy-gl"
        "https://github.com/libcna/easy-gl.git" "develop")
    cna_room_require_checkout("${CNA_ROOT_DIR}/../meta-gl" "meta-gl"
        "https://github.com/libcna/meta-gl.git" "develop")
endif()

# --- CNA build options -------------------------------------------------------
# CNA_CNAEXT is the one that matters: without it every CNA/Graphics/*.hpp header
# compiles to nothing and the whole modern rendering surface this project is
# built on disappears. It defaults OFF upstream, so it is forced here.
set(CNA_CNAEXT           ON  CACHE BOOL   "Enable CNA's extended graphics layer" FORCE)
set(CNA_BUILD_TESTS      OFF CACHE BOOL   "Build CNA's own tests"                FORCE)
set(CNA_BUILD_EXAMPLES   OFF CACHE BOOL   "Build CNA's example applications"     FORCE)
set(CNA_BUILD_C_API      OFF CACHE BOOL   "Build the CNA C API"                  FORCE)
set(CNA_ENABLE_NET       OFF CACHE BOOL   "Build CNA networking"                 FORCE)
set(CNA_ENABLE_VIDEO     OFF CACHE STRING "Enable FFmpeg video playback"         FORCE)
set(EASYGL_BUILD_TESTS    OFF CACHE BOOL "Build easy-gl tests"    FORCE)
set(EASYGL_BUILD_EXAMPLES OFF CACHE BOOL "Build easy-gl examples" FORCE)
set(METAGL_BUILD_TESTS    OFF CACHE BOOL "Build meta-gl tests"    FORCE)

if(CNA_ROOM_RENDERER AND NOT CNA_ROOM_RENDERER STREQUAL "")
    set(CNA_GRAPHICS_RENDERER "${CNA_ROOM_RENDERER}" CACHE STRING
        "Graphics renderer CNA compiles in" FORCE)
endif()

# CNA caches its vendored SDL build inside its own source checkout by default,
# which writes into a dependency this project treats as read-only. Keep it in
# our build tree, keyed by toolchain.
if(NOT DEFINED CNA_SDL_PREBUILT_ROOT)
    set(CNA_SDL_PREBUILT_ROOT
        "${CMAKE_BINARY_DIR}/cna-sdl-prebuilt-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_COMPILER_ID}"
        CACHE PATH "Persistent CNA SDL build cache, kept outside the CNA checkout")
endif()

# --- sharp-runtime component selection ---------------------------------------
# CNA declares the closure it needs itself; cna-room additionally uses
# System.Text.Json (scene/settings files), which has to be enabled *before*
# sharp-runtime is added or its target will not exist.
include("${CNA_ROOT_DIR}/cmake/SharpRuntimeConsumption.cmake" OPTIONAL)
if(DEFINED CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS)
    set(_room_components ${CNA_SHARP_RUNTIME_DEFAULT_COMPONENTS} Text.Json Numerics)
    list(REMOVE_DUPLICATES _room_components)
    set(SHARP_RUNTIME_COMPONENTS "${_room_components}" CACHE STRING
        "Sharp Runtime components required by CNA and cna-room" FORCE)
endif()
