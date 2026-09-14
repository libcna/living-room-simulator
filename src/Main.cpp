// SPDX-License-Identifier: MIT
#include "CnaRoom/RoomApplication.hpp"

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
    try
    {
        CnaRoom::RoomApplication application;
        if (!application.configure(argc, argv)) return 0;
        application.Run();
        return 0;
    }
    catch (const std::exception& failure)
    {
        // Anything that escapes this far is a start-up failure the user needs
        // named rather than a silent exit code: a missing asset, a shader that
        // will not compile, a device the renderer cannot create.
        std::fprintf(stderr, "living-room-simulator: %s\n", failure.what());
        return 1;
    }
    catch (...)
    {
        std::fprintf(stderr, "living-room-simulator: an unknown error occurred during start-up\n");
        return 1;
    }
}
