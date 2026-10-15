cmake_minimum_required(VERSION 3.16)
project(Gable VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Xml PrintSupport)
find_package(OpenGL REQUIRED)

add_library(gablecore STATIC
  src/core/Model.cpp
  src/core/Tessellate.cpp
  src/core/Geometry.cpp
  src/core/Serializer.cpp
  src/core/FileFormats.cpp
  src/core/Solids.cpp
  src/core/Sandbox.cpp
)
target_include_directories(gablecore PUBLIC src)
target_link_libraries(gablecore PUBLIC Qt6::Core Qt6::Gui Qt6::Xml OpenGL::GLU)

if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
file(GLOB APP_SOURCES src/view/*.cpp src/tools/*.cpp src/ui/*.cpp)
add_executable(gable src/main.cpp ${APP_SOURCES})
target_link_libraries(gable PRIVATE gablecore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::PrintSupport OpenGL::GL)
endif()

add_executable(gable_tests tests/geom_tests.cpp)
target_link_libraries(gable_tests PRIVATE gablecore)

if(TARGET gable)
install(TARGETS gable RUNTIME DESTINATION bin)
install(FILES packaging/gable.desktop DESTINATION share/applications)
install(FILES packaging/gable.xml DESTINATION share/mime/packages)
install(FILES resources/icons/gable.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES resources/icons/gable-256.png DESTINATION share/icons/hicolor/256x256/apps RENAME gable.png)
endif()
