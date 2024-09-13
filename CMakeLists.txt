cmake_minimum_required (VERSION 3.10)

project (DxO VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


SET(CMAKE_CXX_FLAGS "-O2 -fPIC -fpermissive")
#SET(CMAKE_C_FLAGS "-O2 -fPIC")

#add_definitions(-fPIC)

find_library(ALSA_LIB asound)
if(NOT ALSA_LIB)
  message(FATAL_ERROR "ALSA library not found")
endif()

# get all python files excluding Mocks
#file(GLOB PYTHON_FILES *.py)
#list(FILTER PYTHON_FILES EXCLUDE REGEX ".*Mock.*")
#list(APPEND PYTHON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Wrapper.pyx")

#execute_process(COMMAND echo "blaaaa")
#execute_process(COMMAND autoconf WORKING_DIRECTORY fftw3 OUTPUT_VARIABLE xxx)
#execute_process(COMMAND "./fftw3/configure --prefix=${CMAKE_BINARY_DIR} --disable-shared --enable-static --enable-float --disable-fortran")

message("xxxx ${xxx}")

#include_directories("App" "/usr/include/python3.6")
#link_directories("/usr/lib" "/usr/local/lib")
message(${CMAKE_BINARY_DIR})

#option (BUILD_SHARED_LIBS "Build shared libraries" OFF)
#option (BUILD_TESTS "Build tests" OFF)
#option (ENABLE_FLOAT "single-precision" ON)
#option (ENABLE_LONG_DOUBLE "long-double precision" OFF)
#option (ENABLE_QUAD_PRECISION "quadruple-precision" OFF)
#option (DISABLE_FORTRAN "Disable Fortran wrapper routines" ON)

#add_definitions(-DFFTW_SINGLE)
#add_subdirectory(fftw-3.3.10 output)

include(ExternalProject)
ExternalProject_Add(
  fftw3
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10
  CMAKE_ARGS
    "-G@CMAKE_GENERATOR@"
    "-C@HUNTER_CACHE_FILE@"
    "-C@HUNTER_ARGS_FILE@"
    "-D${postfix_name}=${${postfix_name}}"
    "-DCMAKE_BUILD_TYPE=${configuration}"
    "-DCMAKE_CONFIGURATION_TYPES=${configuration}"
    "-DCMAKE_INSTALL_PREFIX=@HUNTER_PACKAGE_INSTALL_PREFIX@"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
  CONFIGURE_COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/fftw-3.3.10/configure --host=arm --prefix=${CMAKE_CURRENT_SOURCE_DIR}/fftw3f --enable-static --disable-shared --enable-float --disable-fortran
  BUILD_COMMAND make && make install
  BUILD_IN_SOURCE 1
)
#execute_process(COMMAND ./fftw-3.1.10/configure --prefix=/home/malte/Downloads/fftw-3.3.10 --disable-shared --enable-static --enable-float --disable-fortran && make)
include_directories(fftw3f/include)

#add_library(DxO SHARED plugin.cpp)
add_executable(DxO plugin.cpp)
add_dependencies(DxO fftw3)
set_target_properties(DxO PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(DxO PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
target_link_directories(DxO PUBLIC fftw3f/lib)
target_link_libraries(DxO asound dl libfftw3f.a)

#install(TARGETS DxO DESTINATION /usr/lib/alsa)
