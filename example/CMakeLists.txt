cmake_minimum_required(VERSION 3.14)

project(gremlin_example C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")

# Pass -DGREMLIN_SOURCE_DIR=/path/to/gremlin.c to use a local checkout
# instead of fetching from GitHub.
include(FetchContent)
if(DEFINED GREMLIN_SOURCE_DIR)
    FetchContent_Declare(gremlin SOURCE_DIR ${GREMLIN_SOURCE_DIR})
else()
    FetchContent_Declare(gremlin
        GIT_REPOSITORY https://github.com/norma-core/gremlin.c.git
        GIT_TAG        main
    )
endif()
FetchContent_MakeAvailable(gremlin)

gremlinc_generate(
    TARGET       example_protos
    IMPORTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    PROTOS       example.proto
)

add_executable(example main.c)
target_link_libraries(example PRIVATE example_protos)
