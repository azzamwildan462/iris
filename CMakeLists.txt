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
target_link_libraries(rt_comm 
${LZ4_LIBRARIES} 
z
-pthread
)
set_target_properties(rt_comm PROPERTIES COMPILE_FLAGS "-fPIC")

add_executable(comm_adapter_bs src/comm_adapter_bs.cpp)
target_link_libraries(comm_adapter_bs 
${LZ4_LIBRARIES} 
z
-pthread
)
set_target_properties(comm_adapter_bs PROPERTIES COMPILE_FLAGS "-fPIC")


add_library(LMDB_LIB STATIC IMPORTED)
set_target_properties(LMDB_LIB PROPERTIES
IMPORTED_LOCATION "/home/wildan/proyek/iris/rtdb/lmdb/libraries/liblmdb/liblmdb.a"
INTERFACE_INCLUDE_DIRECTORIES "/home/wildan/proyek/iris/rtdb/lmdb/libraries/liblmdb/"
)

add_executable(iris_rtdb src/iris_rtdb.cpp)
target_link_libraries(iris_rtdb LMDB_LIB)



