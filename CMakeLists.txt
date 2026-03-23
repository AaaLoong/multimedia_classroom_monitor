cmake_minimum_required(VERSION 3.21)
# Qt multimedia classroom monitor
project(multimedia_classroom_monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick Qml QuickControls2 Sql Network Concurrent)
# paho-mqtt-cpp dependency
find_package(PahoMqttCpp REQUIRED)

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(HEAD_DIR ${SRC_ROOT}/head)
set(QML_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/QML)

qt_add_resources(FRONTEND_QML_RESOURCES ${QML_ROOT}/frontend.qrc)
qt_add_resources(SIMULATOR_QML_RESOURCES ${QML_ROOT}/simulator.qrc)

add_library(sim_common STATIC
    src/head/class_init.h
    src/common/class_init.cpp
    src/head/device.h
    src/common/device.cpp
    src/head/topic_defs.h
)
target_link_libraries(sim_common PUBLIC Qt6::Core)
target_include_directories(sim_common PUBLIC ${HEAD_DIR})

add_executable(sim_device_client
    src/device/device_sim_client_main.cpp
    ${SIMULATOR_QML_RESOURCES}
)
target_link_libraries(sim_device_client PRIVATE sim_common Qt6::Core Qt6::Quick Qt6::Qml Qt6::QuickControls2 PahoMqttCpp::paho-mqttpp3)
target_link_libraries(sim_device_client PRIVATE Qt6::Network)

add_library(mqtt_bridge STATIC
    src/head/mqtt_adapter.h
    src/backend/mqtt_adapter.cpp
)
target_link_libraries(mqtt_bridge PUBLIC sim_common Qt6::Core PahoMqttCpp::paho-mqttpp3)
target_compile_definitions(mqtt_bridge PUBLIC PAHO_MQTTPP_IMPORTS)
target_include_directories(mqtt_bridge PUBLIC ${HEAD_DIR})

add_library(qml_shared STATIC
    src/head/devicemodel.h
    src/common/devicemodel.cpp
    src/head/rest_client.h
    src/common/rest_client.cpp
)
target_link_libraries(qml_shared PUBLIC Qt6::Core Qt6::Qml Qt6::Quick Qt6::Network)
target_include_directories(qml_shared PUBLIC ${HEAD_DIR})

add_executable(sim_admin_qml
    src/admin/qml_admin_main.cpp
    ${FRONTEND_QML_RESOURCES}
)
target_link_libraries(sim_admin_qml PRIVATE sim_common qml_shared Qt6::Quick Qt6::Qml Qt6::QuickControls2)

add_executable(sim_classroom_qml
    src/classroom/qml_classroom_main.cpp
    ${FRONTEND_QML_RESOURCES}
)
target_link_libraries(sim_classroom_qml PRIVATE sim_common qml_shared Qt6::Quick Qt6::Qml Qt6::QuickControls2)

add_executable(sim_backend
    src/backend/backend_server.cpp
    src/backend/db_store.cpp
)
target_link_libraries(sim_backend PRIVATE mqtt_bridge Qt6::Core Qt6::Sql Qt6::Network Qt6::Concurrent)
target_include_directories(sim_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)





