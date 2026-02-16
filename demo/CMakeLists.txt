cmake_minimum_required(VERSION 3.16)
project(cea_demo C)

# Build libcea
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/cea_build)

# Find FFmpeg libraries (demuxing only – no avcodec)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)

add_executable(demo demo.c)

target_include_directories(demo PRIVATE
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

# macOS: add Homebrew include paths for FFmpeg headers
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    if(${CMAKE_HOST_SYSTEM_PROCESSOR} MATCHES "arm64")
        target_include_directories(demo PRIVATE /opt/homebrew/include)
    else()
        target_include_directories(demo PRIVATE /usr/local/include)
    endif()
endif()

target_link_directories(demo PRIVATE
    ${AVFORMAT_LIBRARY_DIRS}
    ${AVUTIL_LIBRARY_DIRS}
)

target_link_libraries(demo
    cea
    ${AVFORMAT_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

