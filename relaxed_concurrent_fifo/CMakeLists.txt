# CMakeList.txt : CMake project for relaxed_concurrent_fifo, include source and define
# project specific logic here.
#

set(sources
  main.cpp)

if(NOT MSVC)
   set(sources ${sources} "contenders/2D/2Dd-queue_optimized.c" "contenders/2D/ssalloc.c" "contenders/2D/test.c"
   "contenders/2D/d-balanced-queue.c" "contenders/2D/lcrq.c" "contenders/multififo/util/graph.unix.cpp")
else()
  set(sources ${sources} "contenders/multififo/util/graph.portable.cpp")
endif()
find_package(Threads REQUIRED)
find_package(TBB REQUIRED)
# Add source to this project's executable.
add_executable (relaxed_concurrent_fifo ${sources})
set_target_properties(relaxed_concurrent_fifo PROPERTIES
  CXX_STANDARD 23
)
target_link_libraries(relaxed_concurrent_fifo PRIVATE Threads::Threads TBB::tbb)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET relaxed_concurrent_fifo PROPERTY CXX_STANDARD 20)
endif()

if(MSVC)
  target_compile_options(relaxed_concurrent_fifo PRIVATE /W3 /WX /bigobj)
else()
  target_link_libraries(relaxed_concurrent_fifo PRIVATE atomic)
  target_compile_options(relaxed_concurrent_fifo PRIVATE -Wall -Wextra -Wpedantic)
endif()
