import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../common" as Common

Item {
    id: root

    property bool locked: false
    property bool busy: false
    property bool bulkControlLocked: false
    property bool registered: false
    property bool bindingChecking: false
    property bool backendReady: false
    property string roomId: ""
    property string headerTitle: "教室接入控制台"
    property string inputRoomId: ""
    property string manualRoomText: ""
    property string statusText: ""
    property int deviceCount: 0
    property int onlineCount: 0
    property int activeCount: 0
    property int faultCount: 0
    property int buildingIndex: 0
    property int floorIndex: 0
    property int roomSuffixIndex: 0
    property var buildingOptions
    property var floorOptions
    property var roomSuffixOptions
    property var deviceListModel
    property Component deviceDelegate

    property alias refreshButton: refreshButton
    property alias powerOnAllButton: powerOnAllButton
    property alias powerOffAllButton: powerOffAllButton
    property alias buildingCombo: buildingCombo
    property alias floorCombo: floorCombo
    property alias suffixCombo: suffixCombo
    property alias manualRoomField: manualRoomField
    property alias registerButton: registerButton

    onManualRoomTextChanged: {
        if (!manualRoomField.activeFocus && manualRoomField.text !== manualRoomText)
            manualRoomField.text = manualRoomText
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#f7f9fc" }
            GradientStop { position: 0.5; color: Common.Theme.pageBg }
            GradientStop { position: 1.0; color: "#f8fafc" }
        }

        Rectangle {
            width: root.width * 0.34
            height: root.height * 0.24
            radius: width / 2
            color: "#dbe8ff"
            opacity: 0.18
            x: root.width * 0.66
            y: -height * 0.18
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Common.Theme.spaceXL
        spacing: Common.Theme.spaceL

        Common.SurfaceCard {
            Layout.fillWidth: true
            Layout.preferredHeight: headerContent.implicitHeight + padding * 2

            Column {
                id: headerContent
                width: parent.width
                spacing: Common.Theme.spaceM

                RowLayout {
                    width: parent.width
                    spacing: Common.Theme.spaceM

                    Column {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            text: root.headerTitle
                            color: Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 34
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    Common.StatusChip {
                        text: root.locked ? "已锁定" : (root.registered ? "已接入" : "待接入")
                        toneColor: root.locked ? Common.Theme.danger : (root.registered ? Common.Theme.success : Common.Theme.warning)
                        fillColor: root.locked ? Common.Theme.dangerSoft : (root.registered ? Common.Theme.successSoft : Common.Theme.warningSoft)
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: Common.Theme.spaceM

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: Common.Theme.radiusM
                        color: "#f6f9fc"
                        border.width: 1
                        border.color: Common.Theme.borderColor

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 14
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            text: root.statusText
                            color: Common.Theme.textSecondary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }

                    Common.ActionButton {
                        id: refreshButton
                        text: "刷新状态"
                        variant: "secondary"
                        enabled: root.registered
                    }
                }
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: contentColumn.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: contentColumn
                width: parent.width
                spacing: Common.Theme.spaceL

                Flow {
                    width: parent.width
                    spacing: Common.Theme.spaceL

                    Common.MetricCard {
                        width: contentColumn.width >= 1120 ? (contentColumn.width - Common.Theme.spaceL * 3) / 4 : (contentColumn.width - Common.Theme.spaceL) / 2
                        title: "设备总数"
                        value: String(root.deviceCount)
                        iconText: "总"
                        toneColor: Common.Theme.accent
                        toneFill: Common.Theme.accentSoft
                    }

                    Common.MetricCard {
                        width: contentColumn.width >= 1120 ? (contentColumn.width - Common.Theme.spaceL * 3) / 4 : (contentColumn.width - Common.Theme.spaceL) / 2
                        title: "在线设备"
                        value: String(root.onlineCount)
                        iconText: "在"
                        toneColor: Common.Theme.success
                        toneFill: Common.Theme.successSoft
                    }

                    Common.MetricCard {
                        width: contentColumn.width >= 1120 ? (contentColumn.width - Common.Theme.spaceL * 3) / 4 : (contentColumn.width - Common.Theme.spaceL) / 2
                        title: "活跃设备"
                        value: String(root.activeCount)
                        iconText: "活"
                        toneColor: "#2d7abf"
                        toneFill: "#e9f3ff"
                    }

                    Common.MetricCard {
                        width: contentColumn.width >= 1120 ? (contentColumn.width - Common.Theme.spaceL * 3) / 4 : (contentColumn.width - Common.Theme.spaceL) / 2
                        title: "故障/离线"
                        value: String(root.faultCount)
                        iconText: "警"
                        toneColor: Common.Theme.danger
                        toneFill: Common.Theme.dangerSoft
                    }
                }

                Common.SurfaceCard {
                    width: parent.width
                    height: quickActionsContent.implicitHeight + padding * 2

                    Column {
                        id: quickActionsContent
                        anchors.fill: parent
                        spacing: Common.Theme.spaceL

                        Common.SectionHeader {
                            width: parent.width
                            title: "快捷操作"
                        }

                        Flow {
                            width: parent.width
                            spacing: Common.Theme.spaceM

                            Common.ActionButton {
                                id: powerOnAllButton
                                text: "一键上课"
                                enabled: !root.locked && !root.bulkControlLocked && root.registered && root.deviceCount > 0
                            }

                            Common.ActionButton {
                                id: powerOffAllButton
                                text: "一键下课"
                                enabled: !root.locked && !root.bulkControlLocked && root.registered && root.deviceCount > 0
                            }
                        }
                    }
                }

                Common.SurfaceCard {
                    width: parent.width
                    height: deviceSection.implicitHeight + padding * 2

                    Column {
                        id: deviceSection
                        width: parent.width
                        spacing: Common.Theme.spaceL

                        Common.SectionHeader {
                            width: parent.width
                            title: "设备状态"
                        }

                        ListView {
                            width: parent.width
                            height: contentHeight
                            interactive: false
                            spacing: Common.Theme.spaceM
                            model: root.deviceListModel
                            delegate: root.deviceDelegate
                        }

                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.registered && !root.backendReady
        color: "#660d1625"
        z: 11

        Column {
            anchors.centerIn: parent
            spacing: 10

            BusyIndicator {
                running: root.registered && !root.backendReady
                width: 50
                height: 50
            }

            Text {
                text: "正在同步教室设备..."
                color: "white"
                font.family: Common.Theme.fontFamily
                font.pixelSize: 16
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.bindingChecking
        color: "#cc000000"
        z: 12

        Common.SurfaceCard {
            width: 420
            height: 200
            anchors.centerIn: parent

            Column {
                anchors.centerIn: parent
                width: parent.width - Common.Theme.spaceXL * 2
                spacing: Common.Theme.spaceM

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: root.bindingChecking
                    width: 48
                    height: 48
                }

                Text {
                    width: parent.width
                    text: "正在验证终端身份..."
                    horizontalAlignment: Text.AlignHCenter
                    color: Common.Theme.textPrimary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 24
                    font.bold: true
                }

                Text {
                    width: parent.width
                    text: "系统正在读取当前终端绑定的教室信息"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    color: Common.Theme.textSecondary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 13
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.registered && !root.bindingChecking
        color: "#cc000000"
        z: 12

        Common.SurfaceCard {
            width: 520
            height: 360
            anchors.centerIn: parent

            ColumnLayout {
                anchors.fill: parent
                spacing: Common.Theme.spaceM

                Text {
                    text: "教室接入"
                    color: Common.Theme.textPrimary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 24
                    font.bold: true
                }

                Text {
                    text: "请选择或输入教室号并完成接入注册"
                    color: Common.Theme.textSecondary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 13
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Common.Theme.spaceS

                    ComboBox {
                        id: buildingCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        model: root.buildingOptions
                        currentIndex: root.buildingIndex
                        leftPadding: 16
                        rightPadding: 40
                        topPadding: 14
                        bottomPadding: 14

                        contentItem: Text {
                            text: buildingCombo.displayText
                            color: Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 15
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            radius: Common.Theme.radiusM
                            color: "#f7faff"
                            border.width: 1
                            border.color: buildingCombo.activeFocus ? Common.Theme.accent : "#cfd8e6"
                        }

                        indicator: Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "⌄"
                            color: Common.Theme.textSecondary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 20
                        }

                        popup: Popup {
                            y: buildingCombo.height + 6
                            width: buildingCombo.width
                            padding: 6
                            implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, 220)
                            background: Rectangle {
                                radius: Common.Theme.radiusM
                                color: "white"
                                border.width: 1
                                border.color: "#cfd8e6"
                            }
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: buildingCombo.popup.visible ? buildingCombo.delegateModel : null
                                currentIndex: buildingCombo.highlightedIndex
                                ScrollBar.vertical: ScrollBar {}
                            }
                        }

                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: buildingCombo.width - 12
                            padding: 12
                            highlighted: buildingCombo.highlightedIndex === index
                            contentItem: Text {
                                text: String(modelData)
                                color: Common.Theme.textPrimary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                radius: Common.Theme.radiusS
                                color: parent.highlighted ? "#edf4ff" : "transparent"
                            }
                        }
                    }

                    ComboBox {
                        id: floorCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        model: root.floorOptions
                        currentIndex: root.floorIndex
                        leftPadding: 16
                        rightPadding: 40
                        topPadding: 14
                        bottomPadding: 14

                        contentItem: Text {
                            text: floorCombo.displayText
                            color: Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 15
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            radius: Common.Theme.radiusM
                            color: "#f7faff"
                            border.width: 1
                            border.color: floorCombo.activeFocus ? Common.Theme.accent : "#cfd8e6"
                        }

                        indicator: Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "⌄"
                            color: Common.Theme.textSecondary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 20
                        }

                        popup: Popup {
                            y: floorCombo.height + 6
                            width: floorCombo.width
                            padding: 6
                            implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, 220)
                            background: Rectangle {
                                radius: Common.Theme.radiusM
                                color: "white"
                                border.width: 1
                                border.color: "#cfd8e6"
                            }
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: floorCombo.popup.visible ? floorCombo.delegateModel : null
                                currentIndex: floorCombo.highlightedIndex
                                ScrollBar.vertical: ScrollBar {}
                            }
                        }

                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: floorCombo.width - 12
                            padding: 12
                            highlighted: floorCombo.highlightedIndex === index
                            contentItem: Text {
                                text: String(modelData)
                                color: Common.Theme.textPrimary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                radius: Common.Theme.radiusS
                                color: parent.highlighted ? "#edf4ff" : "transparent"
                            }
                        }
                    }

                    ComboBox {
                        id: suffixCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        model: root.roomSuffixOptions
                        currentIndex: root.roomSuffixIndex
                        leftPadding: 16
                        rightPadding: 40
                        topPadding: 14
                        bottomPadding: 14

                        contentItem: Text {
                            text: suffixCombo.displayText
                            color: Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 15
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            radius: Common.Theme.radiusM
                            color: "#f7faff"
                            border.width: 1
                            border.color: suffixCombo.activeFocus ? Common.Theme.accent : "#cfd8e6"
                        }

                        indicator: Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "⌄"
                            color: Common.Theme.textSecondary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 20
                        }

                        popup: Popup {
                            y: suffixCombo.height + 6
                            width: suffixCombo.width
                            padding: 6
                            implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, 220)
                            background: Rectangle {
                                radius: Common.Theme.radiusM
                                color: "white"
                                border.width: 1
                                border.color: "#cfd8e6"
                            }
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: suffixCombo.popup.visible ? suffixCombo.delegateModel : null
                                currentIndex: suffixCombo.highlightedIndex
                                ScrollBar.vertical: ScrollBar {}
                            }
                        }

                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: suffixCombo.width - 12
                            padding: 12
                            highlighted: suffixCombo.highlightedIndex === index
                            contentItem: Text {
                                text: String(modelData)
                                color: Common.Theme.textPrimary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                radius: Common.Theme.radiusS
                                color: parent.highlighted ? "#edf4ff" : "transparent"
                            }
                        }
                    }
                }

                TextField {
                    id: manualRoomField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    placeholderText: "完整房间号，支持手动输入"
                    color: Common.Theme.textPrimary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 15
                    leftPadding: 16
                    rightPadding: 16
                    topPadding: 14
                    bottomPadding: 14
                    selectByMouse: true

                    background: Rectangle {
                        radius: Common.Theme.radiusM
                        color: "#f7faff"
                        border.width: 1
                        border.color: manualRoomField.activeFocus ? Common.Theme.accent : "#cfd8e6"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 46
                    radius: Common.Theme.radiusM
                    color: "#f8fafc"
                    border.width: 1
                    border.color: Common.Theme.borderColor

                    Text {
                        anchors.centerIn: parent
                        text: "当前教室号: " + root.inputRoomId
                        color: Common.Theme.textPrimary
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true

                    Item { Layout.fillWidth: true }

                    Common.ActionButton {
                        id: registerButton
                        text: "立即接入"
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.locked
        color: "#9d18202e"
        z: 13

        Column {
            anchors.centerIn: parent
            spacing: 8

            Text {
                text: "已锁定"
                color: "white"
                font.family: Common.Theme.fontFamily
                font.pixelSize: 26
                font.bold: true
            }

            Text {
                text: "管理端已接管本教室控制权"
                color: "#d9e1f0"
                font.family: Common.Theme.fontFamily
                font.pixelSize: 14
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.busy
        color: "#44000000"
        z: 14

        Column {
            anchors.centerIn: parent
            spacing: 8

            BusyIndicator {
                running: root.busy
                width: 44
                height: 44
            }

            Text {
                text: "执行中..."
                color: "white"
                font.family: Common.Theme.fontFamily
                font.pixelSize: 14
            }
        }
    }
}
