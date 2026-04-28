cmake_minimum_required(VERSION 3.16)
project(HashServer VERSION 1.0 LANGUAGES CXX)

# Configure C++ Standard (Matches -std=gnu++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Find Dependencies: libssl libcrypto and pthread
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Asio is header-only on Ubuntu, but we need to find its location
find_path(ASIO_INCLUDE_DIR asio.hpp)

# Define the Executable
add_executable(server_epool 
    main.cpp
    # Add other source files here
)

# Include Directories
target_include_directories(server_epool PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ASIO_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
)

# Link Libraries: libssl, libcrypto, pthread
target_link_libraries(server_epool PRIVATE 
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# Compiler Flags: -Wall
target_compile_options(server_epool PRIVATE -Wall)

# CMake handles Debug/Release automatically via CMAKE_BUILD_TYPE.
# If CMAKE_BUILD_TYPE is "Release", it adds -O3 and -DNDEBUG.
# If "Debug", it adds -g.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()


