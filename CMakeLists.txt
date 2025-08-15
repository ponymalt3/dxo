cmake_minimum_required(VERSION 3.18)

project(DxO VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -O3 -mcpu=armv7-a -ftree-vectorize -mfloat-abi=hard -mfpu=neon-vfpv4
find_library(ALSA_LIB asound)

if(NOT ALSA_LIB)
  message(FATAL_ERROR "ALSA library not found")
endif()

if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm")
  add_definitions(-DBUILD_ARM)
  set(BUILD_ARM True)
endif()

add_compile_options(-fPIC -ftree-vectorize -ffast-math -fopt-info-vec-optimized)
add_compile_definitions(PIC)

include(ExternalProject)

if(BUILD_ARM)
  ExternalProject_Add(
    fftw3
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10
    CONFIGURE_COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10/configure --prefix=${CMAKE_CURRENT_SOURCE_DIR}/fftw3f --host=arm --enable-neon --enable-armv7a-cntvct --enable-static --disable-shared --enable-float --disable-fortran
    BUILD_COMMAND make "CFLAGS=-fPIC -ffast-math ${CMAKE_C_FLAGS}" && make install
    BUILD_IN_SOURCE 0
  )
else()
  ExternalProject_Add(
    fftw3
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10
    CONFIGURE_COMMAND
    ${CMAKE_COMMAND} -E env "CFLAGS=-fPIC -O3 -ffast-math"
    ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10/configure --prefix=${CMAKE_CURRENT_SOURCE_DIR}/fftw3f --enable-static --disable-shared --enable-float --disable-fortran
    BUILD_COMMAND make && make install
    BUILD_IN_SOURCE 0
  )
endif()

include_directories(fftw3f/include)
add_library(DxO SHARED alsa_plugin.cpp)
add_dependencies(DxO fftw3)

# set_target_properties(DxO PROPERTIES VERSION ${PROJECT_VERSION})
# set_target_properties(DxO PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(DxO PROPERTIES OUTPUT_NAME "asound_module_pcm_dxo")
target_link_directories(DxO PUBLIC fftw3f/lib)
target_link_libraries(DxO asound dl libfftw3f.a)
install(TARGETS DxO DESTINATION "")

# Unit Tests
if(NOT BUILD_ARM)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  )
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(RunTests alsa_plugin.cpp pcm_stream_tests.cpp alsa_plugin_tests.cpp neon_tests/neon_tests.cpp tasks/test.cpp crossover/test.cpp)
  add_dependencies(RunTests DxO)
  target_link_directories(RunTests PUBLIC fftw3f/lib)
  target_link_libraries(RunTests gtest_main asound libfftw3f.a)

  add_test(NAME gtests COMMAND Testing)

  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(
      TARGET RunTests POST_BUILD
      COMMAND RunTests --gtest_output=xml:Testing.xml
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
  endif()
endif()