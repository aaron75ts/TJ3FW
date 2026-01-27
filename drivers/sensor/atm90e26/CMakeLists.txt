cmake_minimum_required(VERSION 3.20.0)

# zephyr_library()
# zephyr_library_include_directories(.)
# zephyr_library_sources_ifdef(CONFIG_ATM90E26 atm90e26.c)

if(CONFIG_ATM90E26)
  target_sources(app PRIVATE atm90e26.c)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()