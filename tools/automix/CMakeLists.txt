# The automix tuning tool (docs/tasks/automix.md §9); off by default, not shipped.
qt_add_executable(cirrus_automix main.cpp)
target_link_libraries(cirrus_automix PRIVATE cirrus_media)
