add_executable(client-info main.cpp)
target_link_libraries(client-info PRIVATE incppect::incppect)
assign_local_host_root_path("client-info")
