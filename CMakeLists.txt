cmake_minimum_required(VERSION 3.27)

project(DxO VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(CMAKE_CXX_FLAGS "-g -O0 -fPIC -fpermissive")

# -O3 -mcpu=armv7-a -ftree-vectorize -mfloat-abi=hard -mfpu=neon-vfpv4

# SET(CMAKE_C_FLAGS "-O3 -fPIC")
# add_definitions(-fPIC)
find_library(ALSA_LIB asound)

if(NOT ALSA_LIB)
  message(FATAL_ERROR "ALSA library not found")
endif()

# get_cmake_property(_variableNames VARIABLES)
# list(SORT _variableNames)

# foreach(_variableName ${_variableNames})
# message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()
if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm")
  add_definitions(-DBUILD_ARM)
  set(FFTW_CROSS_COMPLIE "--host=arm --enable-neon --enable-armv7a-cntvct")
  message("BUILD FOR ARM")
endif()

include(ExternalProject)
ExternalProject_Add(
  fftw3
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10
  CONFIGURE_COMMAND
  ${CMAKE_COMMAND} -E env "CFLAGS=-fPIC -O3 -fomit-frame-pointer -mtune=native -malign-double -fstrict-aliasing -fno-schedule-insns"
  ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10/configure --host=arm --enable-neon --enable-armv7a-cntvct --prefix=${CMAKE_CURRENT_SOURCE_DIR}/fftw3f --enable-static --disable-shared --enable-float --disable-fortran
  BUILD_COMMAND make && make install
  BUILD_IN_SOURCE 0
)
include_directories(fftw3f/include)

add_library(DxO SHARED alsa_plugin.cpp)

# add_executable(DxO plugin.cpp)
add_compile_definitions(PIC)
add_dependencies(DxO fftw3)

# set_target_properties(DxO PROPERTIES VERSION ${PROJECT_VERSION})
# set_target_properties(DxO PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(DxO PROPERTIES OUTPUT_NAME "asound_module_pcm_dxo")
target_link_directories(DxO PUBLIC fftw3f/lib)
target_link_libraries(DxO asound dl libfftw3f.a)
message(${CMAKE_BINARY_DIR})
install(TARGETS DxO DESTINATION /)

# Unit Tests
enable_testing()
option(INSTALL_GTEST OFF)
option(GTEST_HAS_ABSL OFF)
set(GOOGLETEST_VERSION 1.15.2)
add_subdirectory(googletest)

add_executable(TestingX tests.cpp tasks/test.cpp crossover/test.cpp)
add_dependencies(TestingX DxO)
target_link_directories(TestingX PUBLIC fftw3f/lib)
target_link_libraries(TestingX gtest_main asound libfftw3f.a)

add_test(NAME example_test COMMAND Testing)
add_custom_command(
  TARGET TestingX POST_BUILD
  COMMAND TestingX --gtest_output=xml:Testing.xml
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
