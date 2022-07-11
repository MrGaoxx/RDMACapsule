cmake_minimum_required(VERSION 3.5)
project(RDMACapsule VERSION "0.0")

SET(CMAKE_BUILD_TYPE Debug)
IF(NOT CMAKE_BUILD_TYPE)
 SET(CMAKE_BUILD_TYPE Release)
ENDIF()

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

AUX_SOURCE_DIRECTORY(./common CPP_COMMON)
AUX_SOURCE_DIRECTORY(./core CPP_CORE)
AUX_SOURCE_DIRECTORY(./network CPP_NETWORK)
#AUX_SOURCE_DIRECTORY(./)
INCLUDE_DIRECTORIES(./ ./common ./core ./network)
ADD_LIBRARY(RDMACapsule ${CPP_COMMON} ${CPP_CORE} ${CPP_NETWORK})

#find_path(PATH_LIBVERBS libibverbs.a)
#find_path(PATH_LIBRDMACM librdmacm.a)
#find_path(PATH_LIBBOOSTSYSTEM libboost_system.a)

add_executable(ping_pong_client ./test/ping_pong_client.cc)
target_link_libraries(ping_pong_client RDMACapsule -lpthread -lrdmacm -libverbs -lboost_system)
#target_include_directories(ping_pong_client ./common ./core ./network)

add_executable(ping_pong_server ./test/ping_pong_server.cc)
target_link_libraries(ping_pong_server RDMACapsule -lpthread -lrdmacm -libverbs -lpthread -lboost_system)

add_executable(multicast_daemon ./test/multicast_daemon.cc)
target_link_libraries(multicast_daemon RDMACapsule -lpthread -lrdmacm -libverbs -lpthread -lboost_system)
