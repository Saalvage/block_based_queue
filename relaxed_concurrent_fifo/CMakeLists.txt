﻿# CMakeList.txt : CMake project for relaxed_concurrent_fifo, include source and define
# project specific logic here.
#

set(sources
  main.cpp)

# Add source to this project's executable.
add_executable (relaxed_concurrent_fifo ${sources})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET relaxed_concurrent_fifo PROPERTY CXX_STANDARD 20)
endif()

if(MSVC)
  target_compile_options(relaxed_concurrent_fifo PRIVATE /W3 /WX /bigobj)
else()
  target_compile_options(relaxed_concurrent_fifo PRIVATE -Wall -Wextra -Wpedantic -Wno-interference-size)
endif()
