cmake_minimum_required(VERSION 3.23)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")

project(Uniasset)

include_directories(src)

add_subdirectory(src/uniasset)
add_subdirectory(src/uniasset.test)