cmake_minimum_required(VERSION 3.10)

project(DxO VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
FetchContent_MakeAvailable(googletest)

SET(CMAKE_CXX_FLAGS "-O2 -fPIC -fpermissive")

# SET(CMAKE_C_FLAGS "-O2 -fPIC")

# add_definitions(-fPIC)
find_library(ALSA_LIB asound)

if(NOT ALSA_LIB)
  message(FATAL_ERROR "ALSA library not found")
endif()

# get all python files excluding Mocks
# file(GLOB PYTHON_FILES *.py)
# list(FILTER PYTHON_FILES EXCLUDE REGEX ".*Mock.*")
# list(APPEND PYTHON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Wrapper.pyx")

# execute_process(COMMAND echo "blaaaa")
# execute_process(COMMAND autoconf WORKING_DIRECTORY fftw3 OUTPUT_VARIABLE xxx)
# execute_process(COMMAND "./fftw3/configure --prefix=${CMAKE_BINARY_DIR} --disable-shared --enable-static --enable-float --disable-fortran")
message("xxxx ${xxx}")

include(ExternalProject)
ExternalProject_Add(
  fftw3
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10
  CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10/configure --prefix=${CMAKE_CURRENT_SOURCE_DIR}/fftw3f --enable-static --disable-shared --enable-float --disable-fortran # --host=arm --enable-neon
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
add_executable(Testing tests.cpp tasks/test.cpp)
target_link_libraries(Testing gtest_main asound)
add_test(NAME example_test COMMAND Testing)

add_custom_command(
  TARGET Testing POST_BUILD
  COMMAND Testing --gtest_output=xml:Testing.xml
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)