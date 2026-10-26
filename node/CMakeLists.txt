cmake_minimum_required(VERSION 3.16)

# Shared component directory (relative to this CMakeLists.txt)
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart-agriculture-node)
