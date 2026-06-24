cmake_minimum_required(VERSION 3.14)
project(lru_cache LANGUAGES CXX)

add_library(lru_cache INTERFACE)
target_include_directories(lru_cache INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(lru_cache INTERFACE cxx_std_17)

enable_testing()
add_executable(lru_cache_test test/test.cc)
target_link_libraries(lru_cache_test PRIVATE lru_cache)
add_test(NAME lru_cache COMMAND lru_cache_test)
