cmake_minimum_required (VERSION 3.10)
project (DxO)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


SET(CMAKE_CXX_FLAGS "-O2 -fPIC -fpermissive")
#SET(CMAKE_C_FLAGS "-O2 -fPIC")

#add_definitions(-fPIC)

# get all python files excluding Mocks
#file(GLOB PYTHON_FILES *.py)
#list(FILTER PYTHON_FILES EXCLUDE REGEX ".*Mock.*")
#list(APPEND PYTHON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Wrapper.pyx")

#include_directories("App" "/usr/include/python3.6")
#link_directories("/usr/lib" "/usr/local/lib")

#add_subdirectory(App)

message(${CMAKE_BINARY_DIR})

add_library(DxO plugin.cpp)
target_link_libraries(DxO libsound2)
