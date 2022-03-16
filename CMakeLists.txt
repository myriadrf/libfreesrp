cmake_minimum_required(VERSION 2.8.4)
project(libfreesrp)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "3")
set(VERSION_PATCH "0")
set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

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
)

file(GLOB_RECURSE READERWRITERQUEUE_INCLUDE_FILES
    ${PROJECT_SOURCE_DIR}/src/readerwriterqueue/*.h
)

add_library(
    freesrp
    SHARED
    ${LIBFREESRP_SRC_FILES}
    ${LIBFREESRP_INCLUDE_FILES}
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("-- WARNING: Configured in DEBUG mode. This will *very heavily* impact performance!")
endif()

set_target_properties(freesrp PROPERTIES VERSION ${VERSION} SOVERSION ${VERSION_MAJOR})
target_link_libraries(freesrp ${LIBUSB_1_LIBRARIES})

# Examples
FIND_PACKAGE(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/include)

# freesrp-io, using callbacks instead of busy waiting
set(EXAMPLE_TEST_SOURCE_FILES ${PROJECT_SOURCE_DIR}/examples/io/main.cpp)
add_executable(freesrp-io ${EXAMPLE_TEST_SOURCE_FILES})
target_link_libraries(freesrp-io freesrp pthread)

# freesrp-ctl
file(GLOB_RECURSE FREESRP_CTL_FILES
    ${PROJECT_SOURCE_DIR}/examples/ctl/*.hpp
    ${PROJECT_SOURCE_DIR}/examples/ctl/*.cpp
)
add_executable(freesrp-ctl ${FREESRP_CTL_FILES})
target_link_libraries(freesrp-ctl freesrp)

# Install library
install(TARGETS freesrp LIBRARY DESTINATION lib)
install(FILES ${LIBFREESRP_INCLUDE_FILES} DESTINATION include)

# Install freesrp-ctl and freesrp-io utilities
install(TARGETS freesrp-ctl freesrp-io RUNTIME DESTINATION bin)
