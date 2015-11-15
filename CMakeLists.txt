cmake_minimum_required(VERSION 2.8.4)
project(libfreesrp)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/modules")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

# FreeSRP Library
find_package(libusb-1.0 REQUIRED)

include_directories(${LIBUSB_1_INCLUDE_DIR})

file(GLOB_RECURSE LIBFREESRP_SRC_FILES
    ${PROJECT_SOURCE_DIR}/src/*.hpp
    ${PROJECT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE LIBFREESRP_INCLUDE_FILES
    ${PROJECT_SOURCE_DIR}/include/*.hpp
    ${PROJECT_SOURCE_DIR}/include/*.cpp
)

add_library(
    freesrp
    SHARED
    ${LIBFREESRP_SRC_FILES}
    ${LIBFREESRP_INCLUDE_FILES}
)

target_link_libraries(freesrp ${LIBUSB_1_LIBRARIES})

# Examples
include_directories(${PROJECT_SOURCE_DIR}/include)

# Test app
set(EXAMPLE_TEST_SOURCE_FILES ${PROJECT_SOURCE_DIR}/examples/test/main.cpp)
add_executable(freesrp-test ${EXAMPLE_TEST_SOURCE_FILES})
target_link_libraries(freesrp-test freesrp)

# freesrp-ctl
file(GLOB_RECURSE FREESRP_CTL_FILES
    ${PROJECT_SOURCE_DIR}/examples/ctl/*.hpp
    ${PROJECT_SOURCE_DIR}/examples/ctl/*.cpp
)
add_executable(freesrp-ctl ${FREESRP_CTL_FILES})
target_link_libraries(freesrp-ctl freesrp)