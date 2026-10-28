cmake_minimum_required(VERSION 3.5)
project(taipan CXX)

add_executable(taipan.app main.cpp ../core/taipan_core.cpp)
target_include_directories(taipan.app PRIVATE ../core)
target_link_libraries(taipan.app inkview)
