cmake_minimum_required(VERSION 3.5)
project(RDMACapsule VERSION "0.0")

IF(NOT CMAKE_BUILD_TYPE)
 SET(CMAKE_BUILD_TYPE Release)
ENDIF()

#SET(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3")


MESSAGE("***Build type:" ${CMAKE_BUILD_TYPE})
INCLUDE(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++17" SUPPORTS_CXX17)
IF(SUPPORTS_CXX17)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
MESSAGE(STATUS "the compiler supports c++ 17")
#ELSE
#MESSAGE(FATAL_ERROR "the compiler does not support c++ 17")
ENDIF()

#ADD_SUBDIRECTORY(./build/lib)
file(MAKE_DIRECTORY ./build/lib)
SET(LIBRARY_OUTPUT_PATH ./build/lib/)

AUX_SOURCE_DIRECTORY(./src/common CPP_COMMON)
AUX_SOURCE_DIRECTORY(./src/core CPP_CORE)
AUX_SOURCE_DIRECTORY(./src/network CPP_NETWORK)
AUX_SOURCE_DIRECTORY(./src/multicast CPP_MULTICAST)
AUX_SOURCE_DIRECTORY(./src/controller CPP_CONTROLLER)
INCLUDE_DIRECTORIES(./src/ ./src/common ./src/core ./src/network ./src/multicast ./src/controller /usr/include/python2.7)
ADD_LIBRARY(RDMACapsule ${CPP_COMMON} ${CPP_CORE} ${CPP_NETWORK} ${CPP_MULTICAST} ${CPP_CONTROLLER})

#find_path(PATH_LIBVERBS libibverbs.a)
#find_path(PATH_LIBRDMACM librdmacm.a)
#find_path(PATH_LIBBOOSTSYSTEM libboost_system.a)

add_executable(ping_pong_client ./src/test/ping_pong_client.cc)
target_link_libraries(ping_pong_client RDMACapsule -lpthread -lrdmacm -libverbs -lboost_system)
#target_include_directories(ping_pong_client ./common ./core ./network)

add_executable(ping_pong_server ./src/test/ping_pong_server.cc)
target_link_libraries(ping_pong_server RDMACapsule -lpthread -lrdmacm -libverbs -lpthread -lboost_system)

add_executable(unicast_client ./src/test/unicast_client.cc)
target_link_libraries(unicast_client RDMACapsule -lpthread -lrdmacm -libverbs -lpthread -lboost_system)

add_executable(cr_daemon ./src/test/cr_daemon.cc)
target_link_libraries(cr_daemon RDMACapsule -lpthread -lrdmacm -libverbs -lpthread -lboost_system)

add_executable(multicast_daemon ./src/test/multicast_daemon.cc)
target_link_libraries(multicast_daemon RDMACapsule -lpthread -lrdmacm -libverbs -lpthread -lboost_system -lpython2.7)
