cmake_minimum_required(VERSION 3.16)
project(WonderBox)

set(CMAKE_C_STANDARD 17)

add_subdirectory(NanoGUI)

add_executable(WonderBox main.c)
target_link_libraries(WonderBox PRIVATE NanoGUI)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(WonderBox PUBLIC ${MATH_LIBRARY})
endif()
