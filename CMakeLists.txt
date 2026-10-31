cmake_minimum_required(VERSION 3.10)
project(microcalc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find the ncurses library
find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR} src)

# Define the executable and its source files inside src/
add_executable(microcalc
    src/main.c
    src/formula.c
    src/sheet.c
    src/io.c
    src/ui.c
)

# Link ncurses and the math library (-lm)
target_link_libraries(microcalc PRIVATE ${CURSES_LIBRARIES} m)