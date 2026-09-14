# Compiler warnings for living-room-simulator's own code. Applied to the project's targets
# only; CNA and its dependencies keep their own settings.
include_guard(GLOBAL)

add_library(cna_room_warnings INTERFACE)
add_library(CnaRoom::Warnings ALIAS cna_room_warnings)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(cna_room_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference)
elseif(MSVC)
    target_compile_options(cna_room_warnings INTERFACE /W4 /permissive-)
endif()
