cmake_minimum_required(VERSION 3.16)
project(StockMarketAnalysisSystem)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Gather source files
file(GLOB SRC_SOURCES
        ${PROJECT_SOURCE_DIR}/src/*.cpp
)

# Create the executable target
add_executable(StockMarketAnalysisSystem
        ${SRC_SOURCES}
)


# Specify include directories for the target
target_include_directories(StockMarketAnalysisSystem PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/lib/json
)

# Debugging information
message(STATUS "Source Files: ${SRC_SOURCES}")
message(STATUS "Include Directories: ${PROJECT_SOURCE_DIR}/include, ${PROJECT_SOURCE_DIR}/lib/json")

if(WIN32)
    target_link_libraries(StockMarketAnalysisSystem PRIVATE ws2_32)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(refresh_calculation_tests
        tests/test_refresh_calculations.cpp
        src/PerformanceCalculator.cpp
        src/Mergesort.cpp
        src/Heap.cpp
    )
    target_include_directories(refresh_calculation_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME refresh_calculations COMMAND refresh_calculation_tests)
endif()
