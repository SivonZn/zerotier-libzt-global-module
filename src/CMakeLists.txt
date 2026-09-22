cmake_minimum_required(VERSION 3.18)
project(zt_globald LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIBZT_ROOT "" CACHE PATH "Directory containing include/ZeroTierSockets.h and lib/libzt.so")
if(NOT LIBZT_ROOT)
  message(FATAL_ERROR "Set -DLIBZT_ROOT=/path/to/libzt/android/<abi>")
endif()

add_executable(zt-globald zt-globald.cpp)
target_include_directories(zt-globald PRIVATE "${LIBZT_ROOT}/include")
target_link_directories(zt-globald PRIVATE "${LIBZT_ROOT}/lib")
target_link_libraries(zt-globald PRIVATE zt log)
target_compile_options(zt-globald PRIVATE -fPIE -fvisibility=hidden)
target_link_options(zt-globald PRIVATE -fPIE -pie)
target_link_options(zt-globald PRIVATE "-Wl,-rpath,$ORIGIN/../lib")
