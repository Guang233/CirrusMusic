# Vendored soramanew/m3shapes (MD3 shapes and morphing); see README.md for provenance.
# Upstream builds a shared, installable library with GCC/Clang-only global warnings;
# Cirrus builds the same sources as a static QML module (URI M3Shapes).
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick ShaderTools)

qt_add_library(m3shapes STATIC)

qt_add_qml_module(m3shapes
    URI M3Shapes
    VERSION 1.0
    SOURCES
        include/m3shapes/core/point.hpp
        include/m3shapes/core/utils.hpp
        include/m3shapes/core/cornerrounding.hpp
        include/m3shapes/core/cubic.hpp
        src/core/cubic.cpp
        include/m3shapes/core/feature.hpp
        src/core/feature.cpp
        include/m3shapes/core/roundedpolygon.hpp
        src/core/roundedpolygon.cpp
        include/m3shapes/morph/floatmapping.hpp
        src/morph/floatmapping.cpp
        include/m3shapes/morph/polygonmeasure.hpp
        src/morph/polygonmeasure.cpp
        include/m3shapes/morph/featuremapping.hpp
        src/morph/featuremapping.cpp
        include/m3shapes/morph/morph.hpp
        src/morph/morph.cpp
        include/m3shapes/shapes/shapes.hpp
        src/shapes/shapes.cpp
        include/m3shapes/shapes/materialshapes.hpp
        src/shapes/materialshapes.cpp
        include/m3shapes/qml/materialshapeitem.hpp
        src/qml/materialshapeitem.cpp
        include/m3shapes/qml/smoothshapematerial.hpp
        src/qml/smoothshapematerial.cpp
)

# Embedded at :/m3shapes/shaders/*.qsb (see SmoothShapeShader).
qt_add_shaders(m3shapes "m3shapes_shaders"
    PREFIX "/m3shapes"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES
        src/shaders/smoothshape.vert
        src/shaders/smoothshape.frag
)

target_compile_features(m3shapes PUBLIC cxx_std_20)
target_include_directories(m3shapes SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# qmltyperegistrar includes the registered header by basename.
target_include_directories(m3shapes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/m3shapes/qml")
target_link_libraries(m3shapes PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
