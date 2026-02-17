cmake_minimum_required(VERSION 3.16)

project(cea VERSION 0.1.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Configure version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/cea_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/cea_version.h
    @ONLY
)

# Collect all source files
file(GLOB CEA_SOURCES src/*.c)

# Add library (static or shared depending on BUILD_SHARED_LIBS)
add_library(cea ${CEA_SOURCES})

# Include directories
target_include_directories(cea
    PUBLIC
        # For consumers: installed include path
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>  # generated version header
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src/                               # private source headers
)

# Define export macro for Windows DLLs
target_compile_definitions(cea
    PRIVATE $<$<AND:$<BOOL:${BUILD_SHARED_LIBS}>,$<PLATFORM_ID:Windows>>:CEA_BUILD_DLL>
)

# Installation paths
install(TARGETS cea
    EXPORT ceaTargets
    LIBRARY DESTINATION lib       # .so / .dylib
    ARCHIVE DESTINATION lib       # .a / .lib
    RUNTIME DESTINATION bin       # .dll on Windows
    INCLUDES DESTINATION include
)

# Install headers
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/ DESTINATION include)

# Export CMake targets for find_package()
install(EXPORT ceaTargets
    FILE ceaTargets.cmake
    NAMESPACE cea::
    DESTINATION lib/cmake/cea
)
