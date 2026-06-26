cmake_minimum_required(VERSION 3.20)
project(virtual_tec2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories(.)

# ------------------------------------------------------------------
# tec2_cli : scriptable / MCP-friendly command-line simulator
#   (no curses dependency; this is what the MCP server drives)
# ------------------------------------------------------------------
add_executable(tec2_cli tec2_cli.cpp tec2.cpp chip.cpp bit.cpp)
if (WIN32)
    # static link so the binary runs from any shell without runtime DLLs
    target_link_options(tec2_cli PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# ------------------------------------------------------------------
# monitor : original-style full-screen curses monitor (optional)
# ------------------------------------------------------------------
find_package(Curses)
if (CURSES_FOUND)
    include_directories(${CURSES_INCLUDE_DIRS})
    add_executable(monitor bit.cpp chip.cpp tec2.cpp monitor.cpp)
    target_link_libraries(monitor ${CURSES_LIBRARIES})
else()
    message(STATUS "ncurses not found - skipping the 'monitor' target (tec2_cli still builds)")
endif()
