add_executable(send main.cpp)
target_link_libraries(send PRIVATE incppect::incppect)
assign_local_host_root_path("send")
