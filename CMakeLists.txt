cmake_minimum_required(VERSION 3.14)

project(
  Ramulator
  VERSION 2.0
  LANGUAGES CXX
)

#### Prompt the build type ####
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()
message("Configuring ${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_Version}...")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DRAMULATOR_DEBUG -ggdb")
# set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ##############################
include_directories(${CMAKE_INCLUDE_PATH})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

#### External libraries ####
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Skip updating the external dependencies after populating them for the first time")

message("Configuring yaml-cpp...")
option(YAML_CPP_BUILD_CONTRIB "Enable yaml-cpp contrib in library" OFF)
option(YAML_CPP_BUILD_TOOLS "Enable parse tools" OFF)
option(YAML_BUILD_SHARED_LIBS "Build yaml-cpp as a shared library" OFF)
# find_package(yaml-cpp)
if(NOT yaml-cpp_FOUND)
  FetchContent_Declare(
    yaml-cpp                             
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.7.0                        
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/build/ext/yaml-cpp
  )
  FetchContent_MakeAvailable(yaml-cpp)
  include_directories(${yaml-cpp_SOURCE_DIR}/include)
  message("Done configuring yaml-cpp.")
endif()

message("Configuring spdlog...")
# find_package(spdlog REQUIRED)
if(NOT spdlog_FOUND)
  FetchContent_Declare(
    spdlog                             
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.11.0     
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/build/ext/spdlog
  )
  FetchContent_MakeAvailable(spdlog)
  message("Done configuring spdlog.")
  include_directories(${spdlog_SOURCE_DIR}/include)
  else()
  message("Found spdlog.")
  include_directories(${spdlog_INCLUDE_DIRS})
  add_library(spdlog ALIAS spdlog::spdlog)
  add_compile_definitions(SPDLOG_FMT_EXTERNAL)
endif()



message("Configuring argparse...")
# find_package(argparse)
  if(NOT argparse_FOUND)
  FetchContent_Declare(
      argparse
      GIT_REPOSITORY https://github.com/p-ranav/argparse.git
      GIT_TAG        v2.9     
      SOURCE_DIR     ${CMAKE_SOURCE_DIR}/build/ext/argparse
  )
  FetchContent_MakeAvailable(argparse)
  message("Done configuring argparse.")
  include_directories(${argparse_SOURCE_DIR}/include)
else()
  message("Found argparse.")
  add_library(argparse ALIAS argparse::argparse)
  include_directories(${argparse_INCLUDE_DIRS})
endif()
##################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(ramulator SHARED 
resources/wrappers/ramulator2.cc
resources/wrappers/ramulator2.hh)

target_include_directories(ramulator 
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/resources/wrappers
PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(ramulator PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY  ${PROJECT_SOURCE_DIR}
)
target_link_libraries(
  ramulator 
  PUBLIC yaml-cpp
  PUBLIC spdlog
  PUBLIC ${CONAN_LIBS}
)

add_executable(ramulator-exe)
target_link_libraries(
ramulator-exe
# PRIVATE -Wl,--whole-archive ramulator -Wl,--no-whole-archive
PRIVATE ramulator
PRIVATE argparse
)

set_target_properties(
ramulator-exe
PROPERTIES
OUTPUT_NAME ramulator2
)

add_subdirectory(src)