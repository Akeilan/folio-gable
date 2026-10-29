cmake_minimum_required(VERSION 3.16)
project(Gable VERSION 2.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Xml PrintSupport Svg DBus)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

add_library(gablecore STATIC
  src/core/Model.cpp
  src/core/Tessellate.cpp
  src/core/Geometry.cpp
  src/core/Serializer.cpp
  src/core/FileFormats.cpp
  src/core/Import2.cpp
  src/core/Export2.cpp
  src/core/Solids.cpp
  src/core/Sandbox.cpp
  src/core/Dynamic.cpp
)
target_include_directories(gablecore PUBLIC src)
target_link_libraries(gablecore PUBLIC Qt6::Core Qt6::Gui Qt6::Xml OpenGL::GLU ZLIB::ZLIB)
target_compile_definitions(gablecore PRIVATE GABLE_VERSION="${PROJECT_VERSION}")

if(EXISTS ${CMAKE_SOURCE_DIR}/src/main.cpp)
file(GLOB APP_SOURCES src/view/*.cpp src/tools/*.cpp src/ui/*.cpp)
add_executable(gable src/main.cpp ${APP_SOURCES})
set_target_properties(gable PROPERTIES OUTPUT_NAME folio-gable)
target_link_libraries(gable PRIVATE gablecore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::PrintSupport Qt6::Svg Qt6::DBus OpenGL::GL)
target_compile_definitions(gable PRIVATE GABLE_VERSION="${PROJECT_VERSION}")
file(GLOB RIBBON_ICONS RELATIVE ${CMAKE_SOURCE_DIR}/resources/ribbon ${CMAKE_SOURCE_DIR}/resources/ribbon/*.svg)
list(TRANSFORM RIBBON_ICONS PREPEND resources/ribbon/)
qt_add_resources(gable "ribbon" PREFIX "/ribbon" BASE resources/ribbon FILES ${RIBBON_ICONS})
qt_add_resources(gable "appicon" PREFIX "/icons" BASE resources/icons FILES resources/icons/folio-gable.svg)
endif()

add_executable(gable_tests tests/geom_tests.cpp)
target_link_libraries(gable_tests PRIVATE gablecore)

if(TARGET gable)
install(TARGETS gable RUNTIME DESTINATION bin)
install(FILES packaging/folio-gable.desktop DESTINATION share/applications)
install(FILES packaging/folio-gable.xml DESTINATION share/mime/packages)
install(FILES resources/icons/folio-gable.svg DESTINATION share/icons/hicolor/scalable/apps)
foreach(sz 16 24 32 48 64 128 256)
  install(FILES resources/icons/folio-gable-${sz}.png DESTINATION share/icons/hicolor/${sz}x${sz}/apps RENAME folio-gable.png)
endforeach()
endif()
