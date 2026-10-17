find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick)

# One output tree for every UI QML module so qmllint and tooling resolve imports.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

add_subdirectory(theme)
add_subdirectory(controls)
add_subdirectory(samples)
add_subdirectory(app)

# Development preview of themes, controls and pages; never part of a release package.
option(CIRRUS_BUILD_GALLERY "Build the UI gallery (appCirrusGallery)" ON)
if(CIRRUS_BUILD_GALLERY)
    add_subdirectory(gallery)
endif()
