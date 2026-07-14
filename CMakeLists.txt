cmake_minimum_required(VERSION 3.20)
project(chronocore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(chronocore_core
  src/baseline_store.cpp
  src/correlation_engine.cpp
  src/shared_marker_source.cpp
  src/shared_ring.cpp
  src/synthetic_source.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(chronocore_core PRIVATE src/linux/perf_collector.cpp)
else()
  target_sources(chronocore_core PRIVATE src/perf_collector_stub.cpp)
endif()
target_include_directories(chronocore_core PUBLIC include)
target_compile_options(chronocore_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(chronocore-daemon src/main.cpp src/http_server.cpp)
target_link_libraries(chronocore-daemon PRIVATE chronocore_core)
target_compile_options(chronocore-daemon PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_executable(chronocore-tests tests/test_statistics.cpp)
target_link_libraries(chronocore-tests PRIVATE chronocore_core)
target_compile_options(chronocore-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME chronocore-tests COMMAND chronocore-tests)

add_executable(chronocore-example-engine examples/matching_engine.cpp)
target_link_libraries(chronocore-example-engine PRIVATE chronocore_core)
target_compile_options(chronocore-example-engine PRIVATE -Wall -Wextra -Wpedantic -Werror)
