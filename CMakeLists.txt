cmake_minimum_required(VERSION 3.25)
project(WonderBox)

set(CMAKE_C_STANDARD 17)

add_subdirectory(NanoGUI)

add_executable(WonderBox main.c)
target_link_libraries(WonderBox PRIVATE NanoGUI)
