cmake_minimum_required(VERSION 3.10)
project(Gitlite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# Add source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Create executable
add_executable(gitlite ${SOURCES} main.cpp)

# Set output directory
set_target_properties(gitlite PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Compiler flags
target_compile_options(gitlite PRIVATE -Wall -Wextra -g)