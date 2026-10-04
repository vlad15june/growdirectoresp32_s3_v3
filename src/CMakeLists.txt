# The application as one ESP-IDF component, for the esp32-s3-devkitm-1-src
# environment only - the prebuilt-framework environments compile src/ through
# PlatformIO's Arduino scripts and never read this file.
#
# The selection mirrors what those environments build: every C/C++ translation
# unit under src/, recursively, with src/ itself on the include path (headers
# in this tree are included as "Core/..." / "Infrastructure/..." relative to
# src/, which PlatformIO serves by putting the source directory on CPPPATH).
FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Sorted explicitly: the glob's order is a filesystem detail, and the file list
# becomes the compile and link order - reproducible builds need it to be a
# function of the file set, not of directory enumeration.
list(SORT app_sources)

idf_component_register(SRCS ${app_sources} INCLUDE_DIRS ".")

# The prebuilt-framework environments compile these sources with the Arduino
# toolchain flags, where a warning is not an error. ESP-IDF adds -Werror=all
# globally; keeping it fatal here would demand source changes for warnings the
# production path accepts, and changing sources is exactly what this build must
# not do. GCC's blanket -Wno-error does not demote what -Werror=all promoted, so
# each diagnostic this codebase currently triggers is demoted individually - the
# same mechanism ESP-IDF's own -Wno-error=unused-function entries use. A future
# change that introduces a new warning kind will fail this environment loudly;
# extend the list, or fix the warning, deliberately. Scoped to this component
# only - the framework components keep ESP-IDF's flags - and diagnostic flags do
# not change code generation.
# PlatformIO compiles the registered libraries (lib_deps) with this component's
# flag set, so the list below also covers the vendored third-party libraries -
# return-type, for one, fires inside the pinned Telaire T6700 driver.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=reorder
    -Wno-error=switch
    -Wno-error=format-overflow
    -Wno-error=return-type
)
