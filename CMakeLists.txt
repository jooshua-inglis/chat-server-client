cmake_minimum_required(VERSION 3.15)
project(Chat)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(server
        server.c
        server.h

        mutex.c
        mutex.h
        util.c
        util.h
        )
add_executable(client
        client.c
        client.h
        util.c
        util.h
        )

target_link_libraries(client pthread rt)
target_link_libraries(server pthread rt)