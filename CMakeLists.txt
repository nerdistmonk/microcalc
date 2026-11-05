cmake_minimum_required(VERSION 3.10)
project(microcalc C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find the ncurses library
find_package(Curses REQUIRED)

# Gnumeric import: zlib for gzip-compressed files, expat for the XML
find_package(ZLIB REQUIRED)
find_path(EXPAT_INCLUDE_DIR expat.h)
find_library(EXPAT_LIBRARY expat)
if(NOT EXPAT_INCLUDE_DIR OR NOT EXPAT_LIBRARY)
    message(FATAL_ERROR "expat development files not found (install libexpat1-dev)")
endif()

include_directories(${CURSES_INCLUDE_DIR} ${EXPAT_INCLUDE_DIR} src)

# Define the executable and its source files inside src/
add_executable(microcalc
    src/main.c
    src/formula.c
    src/sheet.c
    src/io.c
    src/ui.c
    src/gnumeric.c
)

# Link ncurses, zlib, expat and the math library (-lm)
target_link_libraries(microcalc PRIVATE ${CURSES_LIBRARIES} ${ZLIB_LIBRARIES} ${EXPAT_LIBRARY} m)