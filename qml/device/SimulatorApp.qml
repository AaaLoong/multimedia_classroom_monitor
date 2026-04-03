import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    visible: true
    width: 620
    height: 360
    title: "设备仿真终端"
    color: "black"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Label {
            text: sim.currentRoomId === ""
                  ? "多教室设备仿真"
                  : "教室 " + sim.currentRoomId + " 设备仿真"
            font.pixelSize: 22
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "教室" }
            ComboBox {
                id: roomBox
                Layout.preferredWidth: 180
                model: sim.roomIds
                enabled: count > 0
                onActivated: sim.currentRoomId = currentText

                function syncSelection() {
                    if (sim.currentRoomId === "") {
                        currentIndex = -1
                        return
                    }
                    const idx = find(sim.currentRoomId)
                    if (idx >= 0 && currentIndex !== idx)
                        currentIndex = idx
                }

                Component.onCompleted: syncSelection()
                onModelChanged: syncSelection()

                Connections {
                    target: sim
                    function onCurrentRoomIdChanged() { roomBox.syncSelection() }
                }
            }

            Button {
                text: "刷新教室"
                onClicked: sim.refreshRooms()
            }

            Button {
                text: sim.roomOperationBusy ? "处理中..." : "取消注册"
                enabled: sim.currentRoomId !== "" && !sim.roomOperationBusy
                onClicked: sim.unregisterCurrentRoom()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: "设备" }
            ComboBox {
                id: deviceBox
                Layout.preferredWidth: 180
                model: sim.deviceIds
                enabled: count > 0
                onActivated: sim.selectedDeviceId = currentText

                function syncSelection() {
                    if (sim.selectedDeviceId === "") {
                        currentIndex = -1
                        return
                    }
                    const idx = find(sim.selectedDeviceId)
                    if (idx >= 0 && currentIndex !== idx)
                        currentIndex = idx
                }

                Component.onCompleted: syncSelection()
                onModelChanged: syncSelection()

                Connections {
                    target: sim
                    function onSelectedDeviceIdChanged() { deviceBox.syncSelection() }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: "切换设备离线"
                onClicked: sim.toggleSelectedDeviceOffline()
            }

            Button {
                text: sim.randomDelayEnabled ? "关闭随机延迟" : "开启随机延迟"
                onClicked: sim.toggleRandomDelay()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "white"
            border.color: "#d8dee9"
            radius: 8

            Text {
                anchors.fill: parent
                anchors.margins: 10
                wrapMode: Text.Wrap
                text: "当前教室: " + (sim.currentRoomId === "" ? "未接入" : sim.currentRoomId)
                      + "\n设备数: " + sim.deviceIds.length
                      + "\n状态: " + sim.statusText
                      + "\n随机延迟: " + (sim.randomDelayEnabled ? "已开启" : "已关闭")
                      + "\n最近延迟: " + sim.lastDelayMs + " ms"
                color: "#334155"
                font.pixelSize: 14
            }
        }
    }
}
