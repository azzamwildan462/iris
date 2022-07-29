CMAKE_MINIMUM_REQUIRED(VERSION 3.20)
PROJECT( IRISCOMM )

SET( CMAKE_MODULE_PATH ${CMAKE_MODULES_PATH} ${CMAKE_SOURCE_DIR}/CMakeModules )

find_package(LZ4 REQUIRED)
if(LZ4_FOUND)
    include_directories(SYSTEM ${LZ4_INCLUDE_DIR})
endif()

# find_package(yaml-cpp REQUIRED)
# find_package(Boost COMPONENTS system filesystem REQUIRED)

add_executable(rt_comm src/rt_comm.cpp)
# add_dependencies(rt_comm lz4)
target_link_libraries(rt_comm 
${LZ4_LIBRARIES} 
z
-pthread
)
set_target_properties(rt_comm PROPERTIES COMPILE_FLAGS "-fPIC")

