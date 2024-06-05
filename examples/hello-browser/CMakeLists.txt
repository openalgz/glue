add_executable(hello-browser main.cpp)
target_link_libraries(hello-browser PRIVATE incppect::incppect)
assign_local_host_root_path("hello-browser")
