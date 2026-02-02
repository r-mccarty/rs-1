# RS-1 HardwareOS Firmware
# ESP-IDF 5.x Project

cmake_minimum_required(VERSION 3.16)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Project configuration
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define project
project(rs1_firmware VERSION 0.1.0 LANGUAGES C)

# Custom targets
add_custom_target(docs
    COMMAND doxygen ${CMAKE_CURRENT_LIST_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Generating documentation"
)
