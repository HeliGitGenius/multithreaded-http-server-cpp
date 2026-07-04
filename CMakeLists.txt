cmake_minimum_required(VERSION 3.10)
project(http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the caller did not choose a build type.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The project must compile warning-clean under this exact set of flags.
add_compile_options(-Wall -Wextra -Werror -pedantic)

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

add_executable(http_server
    src/main.cpp
    src/config.cpp
    src/logger.cpp
    src/socket.cpp
    src/thread_pool.cpp
    src/http_request.cpp
    src/http_response.cpp
    src/server.cpp
    src/load_balancer.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(http_server PRIVATE Threads::Threads)
