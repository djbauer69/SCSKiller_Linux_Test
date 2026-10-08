cmake_minimum_required(VERSION 3.20)
project(SCSKillerKde LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(KF6Kirigami REQUIRED)

qt_standard_project_setup()

qt_add_executable(scskiller-kde main.cpp)

qt_add_qml_module(scskiller-kde
    URI SCSKiller.Kde
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(scskiller-kde PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    KF6::Kirigami
)