cmake_minimum_required (VERSION 3.10)
project (DxO)

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

#include_directories("App" "/usr/include/python3.6")
#link_directories("/usr/lib" "/usr/local/lib")

#add_subdirectory(App)

message(${CMAKE_BINARY_DIR})

add_library(DxO SHARED plugin.cpp)
#add_executable(DxO plugin.cpp)
set_target_properties(DxO PROPERTIES VERSION "1.0.0")
#set_target_properties(DxO PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
target_link_libraries(DxO asound dl)

install(TARGETS DxO DESTINATION /usr/lib/alsa)
