cmake_minimum_required(VERSION 3.24)
project(project)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(project
        http_conn.cpp
        http_conn.h
        locker.h
        main.cpp
        threadpool.h)
