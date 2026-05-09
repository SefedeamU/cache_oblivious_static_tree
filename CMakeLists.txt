cmake_minimum_required(VERSION 3.16)
project(cache_oblivious_static_tree LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cache_tree_lib
    src/cache_oblivious_static_tree.cpp
)
target_include_directories(cache_tree_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(cpp_qt_learn main.cpp)
target_link_libraries(cpp_qt_learn PRIVATE cache_tree_lib)

add_executable(cache_tree_tests tests/cache_tree_tests.cpp)
target_link_libraries(cache_tree_tests PRIVATE cache_tree_lib)

enable_testing()
add_test(NAME cache_tree_tests COMMAND cache_tree_tests)
