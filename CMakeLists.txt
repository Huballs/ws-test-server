cmake_minimum_required(VERSION 3.11)
set(CMAKE_VERBOSE_MAKEFILE ON)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CXX_STANDARD_REQUIRED ON)
set(CXX_EXTENSIONS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-all-candidates")

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_BINARY_DIR}/conan_toolchain.cmake)

set(PROJECT_NAME ws-test-client)

project(${PROJECT_NAME} CXX)

# include(${CMAKE_BINARY_DIR}/conanbuildinfo_multi.cmake)
# conan_basic_setup(TARGETS)

#set(Boost_USE_STATIC_LIBS ON)

find_package(Boost REQUIRED)

if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
  #set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -D_WIN32_WINNT=0x0A00") # windows 10
endif()

set(SRC_FILES
  src/main.cpp
  src/util.cpp
  src/device_payloads.cpp
  src/device_payloads.hpp
  src/coro_read.hpp
  )

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC boost::boost)
target_link_libraries(${PROJECT_NAME} PUBLIC boost::boost)