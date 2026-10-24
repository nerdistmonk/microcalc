cmake_minimum_required(VERSION 3.16)
project(microcalc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find ncurses package
find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncurses)

include_directories(${NCURSES_INCLUDE_DIRS})

add_executable(microcalc src/main.c)

target_link_libraries(microcalc PRIVATE ${NCURSES_LIBRARIES})
