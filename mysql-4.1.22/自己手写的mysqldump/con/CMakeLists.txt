cmake_minimum_required(VERSION 3.16)
project(con)

set(CMAKE_CXX_STANDARD 14)
include_directories(/usr/include/mysql/)
link_directories(/usr/lib/x86_64-linux-gnu/)
add_executable(con main.cpp)
target_link_libraries(${PROJECT_NAME} libmysqlclient.so)