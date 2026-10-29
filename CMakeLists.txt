cmake_minimum_required(VERSION 3.16)
project(kamera VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(QT_DEFAULT_MAJOR_VERSION 6)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6Core REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Quick
    QuickControls2
    Sql
    Concurrent
)

find_package(KF6 REQUIRED COMPONENTS
    KIO
    I18n
    Notifications
)

add_executable(kamera
    src/main.cpp
    src/gallerymodel.cpp
    src/thumbnailprovider.cpp
    src/fileops.cpp
    src/imagediskcache.cpp
    src/logger.cpp
    src/videoutils.cpp
)

target_link_libraries(kamera
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Sql
        Qt6::Concurrent
        KF6::KIOCore
        KF6::KIOGui
        KF6::KIOWidgets
        KF6::KIOFileWidgets
        KF6::I18n
        KF6::Notifications
)

# Theme.qml is a singleton QML type.
# We must set QT_QML_SINGLETON_TYPE on qml/Theme.qml before qt_add_qml_module
# so that the generated qmldir includes "singleton Theme 1.0 qml/Theme.qml".
set_source_files_properties(qml/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(kamera
    URI kamera
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml_modules/kamera"
    QML_FILES
        qml/main.qml
        qml/Theme.qml
        qml/PhotoGrid.qml
        qml/PhotoViewer.qml
        qml/Sidebar.qml
        qml/TopBar.qml
)

install(TARGETS kamera DESTINATION ${KDE_INSTALL_BINDIR})
