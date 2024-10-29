cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
list(APPEND EXTRA_COMPONENT_DIRS ./lib/esp32-camera ./lib/i2c_manager ./lib/bm8563 ./lib/m5stack-camera)
project(camera_m5_idf)
