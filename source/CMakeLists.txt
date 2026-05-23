cmake_minimum_required(VERSION 3.16)
project(mbo_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MBO_CORE_SOURCES
    pcap_io.cpp
    flex_protocol.cpp
    flex_tags.cpp
    order_book.cpp
    jpx_tick.cpp
    venue_loader.cpp
    auction.cpp
    pipeline.cpp
)

add_library(mbo_core STATIC ${MBO_CORE_SOURCES})
target_include_directories(mbo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(pcap_reader main.cpp)
target_link_libraries(pcap_reader PRIVATE mbo_core)
set_target_properties(pcap_reader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(mbo_core PRIVATE /W4)
    target_compile_options(pcap_reader PRIVATE /W4)
else()
    target_compile_options(mbo_core PRIVATE -Wall -Wextra)
    target_compile_options(pcap_reader PRIVATE -Wall -Wextra)
endif()
