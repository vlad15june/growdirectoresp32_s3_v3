# ESP-IDF project file for the esp32-s3-devkitm-1-src environment, which builds
# the Arduino framework from source as an ESP-IDF component instead of linking
# the prebuilt SDK. Only that environment reads this file: the prebuilt-framework
# environments and the native test environment build through PlatformIO's own
# scripts and never invoke CMake. The application itself is registered as a
# component in src/CMakeLists.txt.
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(growdirectoresp32_s3_v3)
