enable_testing()

set(MBO_TEST_SOURCES
    test_auction.cpp
    test_venue.cpp
    test_pcap_io.cpp
    test_order_book.cpp
    test_jpx_tick.cpp
    test_pipeline.cpp
)

foreach(test_src ${MBO_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE mbo_core)
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
    )
    if(MSVC)
        target_compile_options(${test_name} PRIVATE /W4)
    else()
        target_compile_options(${test_name} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
