add_executable(hello-browser-ssl main.cpp)
target_link_libraries(hello-browser-ssl PRIVATE incppect::incppect)
assign_local_host_root_path("hello-browser-ssl")
