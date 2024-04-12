cmake_minimum_required(VERSION 3.23)

set(ENV{IPHONEOS_DEPLOYMENT_TARGET} "11.0")

project(Uniasset)

include_directories(src)

add_subdirectory(src/uniasset)
add_subdirectory(src/uniasset.test)