hide_warnings()

add_executable("balls2d" main.cpp)
target_link_libraries("balls2d" PRIVATE incppect::incppect)
assign_local_host_root_path("balls2d")
