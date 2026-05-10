cmake_minimum_required(VERSION 3.10)
project(GLAD VERSION 0.1.0 LANGUAGES C)

add_library(${PROJECT_NAME} STATIC src/glad.c)
target_include_directories(${PROJECT_NAME} PRIVATE include)
