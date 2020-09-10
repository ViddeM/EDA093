cmake_minimum_required(VERSION 3.17)
project(EDA093 C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(EDA093
        lsh.c
        parse.c
        parse.h)
