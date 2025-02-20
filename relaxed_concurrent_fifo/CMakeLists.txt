# CMakeList.txt : CMake project for relaxed_concurrent_fifo, include source and define
# project specific logic here.
#

set(sources
  contenders/2D/2Dd-queue_optimized.c
  contenders/2D/ssalloc.c
  contenders/2D/test.c
  main.cpp)

# Add source to this project's executable.
add_executable (relaxed_concurrent_fifo ${sources})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET relaxed_concurrent_fifo PROPERTY CXX_STANDARD 20)
endif()

target_link_libraries(relaxed_concurrent_fifo PRIVATE atomic)

if(MSVC)
  target_compile_options(relaxed_concurrent_fifo PRIVATE /W3 /WX /bigobj)
else()
  target_compile_options(relaxed_concurrent_fifo PRIVATE -Wall -Wextra -Wpedantic -Wno-interference-size)
endif()
