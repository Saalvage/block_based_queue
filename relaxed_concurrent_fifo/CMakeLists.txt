# CMakeList.txt : CMake project for relaxed_concurrent_fifo, include source and define
# project specific logic here.
#

set(sources
  main.cpp
  contenders/multififo/timestamp.cpp)

if(NOT MSVC)
  if(NOT BBQ_IS_ARM)
    set(sources ${sources} "contenders/2D/2Dd-queue_optimized.c" "contenders/2D/ssalloc.c" "contenders/2D/test.c"
      "contenders/2D/d-balanced-queue.c" "contenders/2D/lcrq.c")
  endif()
  set(sources ${sources} "contenders/multififo/util/graph.unix.cpp")
  find_package(Threads REQUIRED)
  find_package(TBB REQUIRED)

  if(NOT PAPI_FOUND)
    message(STATUS "PAPI library not found, performance counters not available")
  endif()
else()
  set(sources ${sources} "contenders/multififo/util/graph.portable.cpp")
endif()

# Add source to this project's executable.
add_executable (relaxed_concurrent_fifo ${sources})
set_target_properties(relaxed_concurrent_fifo PROPERTIES
  CXX_STANDARD 23
)

if(PAPI_FOUND)
  target_link_libraries(relaxed_concurrent_fifo INTERFACE PAPI::PAPI)
  target_compile_definitions(relaxed_concurrent_fifo INTERFACE WITH_PAPI)
  add_compile_definitions(relaxed_concurrent_fifo WITH_PAPI)
  message(STATUS "HALLO???")
endif()

if(NOT MSVC)
  target_link_libraries(relaxed_concurrent_fifo PRIVATE Threads::Threads TBB::tbb)
endif()

target_include_directories(relaxed_concurrent_fifo PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/contenders/multififo
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET relaxed_concurrent_fifo PROPERTY CXX_STANDARD 20)
endif()

if(MSVC)
  target_compile_options(relaxed_concurrent_fifo PRIVATE /W3 /WX /bigobj)
else()
  target_link_libraries(relaxed_concurrent_fifo PRIVATE atomic)
  target_compile_options(relaxed_concurrent_fifo PRIVATE -Wall -Wextra -Wpedantic)
endif()
