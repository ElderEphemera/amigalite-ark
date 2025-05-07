cmake_minimum_required(VERSION 3.11)
project(amigalite-ark)

# Dependencies
find_package(raylib 5.5 REQUIRED)

# Project

add_executable(${PROJECT_NAME} main.c)
target_link_libraries(${PROJECT_NAME} raylib m)
install(TARGETS ${PROJECT_NAME})

# Web Configurations
if (${PLATFORM} STREQUAL "Web")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -sUSE_GLFW=3 -sASSERTIONS=1 -sWASM=1 -sASYNCIFY -sGL_ENABLE_GET_PROC_ADDRESS=1 -sALLOW_MEMORY_GROWTH -sFORCE_FILESYSTEM")
endif()
