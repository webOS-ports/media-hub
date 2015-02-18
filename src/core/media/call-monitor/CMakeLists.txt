SET (CMAKE_INCLUDE_CURRENT_DIR ON)
SET (CMAKE_AUTOMOC ON)

find_package(Qt5Core REQUIRED)
find_package(PkgConfig REQUIRED)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -pedantic -Wextra -fPIC -pthread")

include_directories(
    ${TP_QT5_INCLUDE_DIRS}
)

add_library(call-monitor STATIC
    call_monitor.cpp
)

qt5_use_modules(call-monitor Core DBus)
