add_executable(balls3d main.cpp)
target_link_libraries(balls3d PRIVATE incppect::incppect)
assign_local_host_root_path("balls3d")