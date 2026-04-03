import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../common" as Common

Item {
    id: root

    property bool loggedIn: false
    property bool locked: false
    property bool busy: false
    property bool isSuperAdmin: false
    property bool canChangePassword: false
    property int navIndex: 0
    property int selectedDeviceIndex: -1
    property bool hasSelectedDevice: false
    property string pageTitle: "系统总览"
    property string pageDateText: ""
    property string statusText: ""
    property string adminUsername: ""
    property string overviewHeadline: ""
    property string overviewFootnote: ""
    property int roomCount: 0
    property int activeRoomCount: 0
    property int onlineDeviceCount: 0
    property int totalDeviceCount: 0
    property int alertCount: 0
    property string devicePageMode: "rooms"
    property string selectedRoomId: ""
    property var roomSummaryListModel
    property var alertPreviewModel
    property var alertListModel
    property var deviceListModel
    property Component deviceListDelegate

    property alias navOverviewButton: overviewNavButton
    property alias navDeviceButton: deviceNavButton
    property alias navAlertButton: alertNavButton
    property alias syncButton: syncButton
    property alias lockButton: lockButton
    property alias manageAdminsButton: manageAdminsButton
    property alias changePasswordButton: changePasswordButton
    property alias viewAllAlertsButton: viewAllAlertsButton
    property alias loginButton: loginButton
    property alias logoutButton: logoutButton
    property alias userField: userField
    property alias passField: passField
    property alias roomListView: roomListView
    property alias deviceListView: deviceListView

    signal roomSelected(string roomId)
    signal backToRoomsRequested()
    signal refreshDevicePageRequested()
    signal powerOnRequested()
    signal powerOffRequested()
    signal processAlertRequested(int alertId)
    signal processAllAlertsRequested

    readonly property int sidebarWidth: 276
    readonly property int pageGap: Common.Theme.spaceL

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: "#f7f9fc"
            }
            GradientStop {
                position: 0.5
                color: Common.Theme.pageBg
            }
            GradientStop {
                position: 1.0
                color: "#f8fafc"
            }
        }

        Rectangle {
            width: root.width * 0.38
            height: root.height * 0.26
            radius: width / 2
            color: "#dbe8ff"
            opacity: 0.22
            x: root.width * 0.62
            y: -height * 0.22
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        visible: root.loggedIn
        enabled: root.loggedIn

        Rectangle {
            Layout.preferredWidth: root.sidebarWidth
            Layout.fillHeight: true
            color: "#f8fbff"
            border.color: Common.Theme.borderColor

            Column {
                anchors.fill: parent
                anchors.margins: Common.Theme.spaceXL
                spacing: Common.Theme.spaceL

                Common.SurfaceCard {
                    width: parent.width
                    height: 124
                    padding: Common.Theme.spaceXL

                    Row {
                        anchors.fill: parent
                        spacing: Common.Theme.spaceL

                        Rectangle {
                            width: 60
                            height: 60
                            radius: 22
                            color: Common.Theme.accent
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "MM"
                                color: "white"
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            Text {
                                text: "教室总控"
                                color: Common.Theme.textPrimary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 22
                                font.bold: true
                            }
                        }
                    }
                }

                Button {
                    id: overviewNavButton
                    width: parent.width
                    height: 80
                    checkable: true
                    hoverEnabled: enabled
                    checked: root.navIndex === 0
                    transformOrigin: Item.Center
                    scale: overviewNavButton.enabled
                           ? (overviewNavButton.down ? 0.985 : (overviewNavButton.hovered ? 1.01 : 1.0))
                           : 1.0

                    Behavior on scale {
                        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
                    }

                    background: Rectangle {
                        radius: 24
                        color: overviewNavButton.checked
                               ? (overviewNavButton.down ? "#1f4ea9" : (overviewNavButton.hovered ? "#255bc2" : Common.Theme.accent))
                               : (overviewNavButton.down ? "#eef3fa" : (overviewNavButton.hovered ? "#f4f8ff" : "#fcfdff"))
                        border.width: 1
                        border.color: overviewNavButton.checked
                                      ? (overviewNavButton.hovered ? "#5b87ea" : Common.Theme.accent)
                                      : (overviewNavButton.hovered ? "#9fbaf0" : Common.Theme.borderColor)

                        Behavior on color {
                            ColorAnimation { duration: 120 }
                        }

                        Behavior on border.color {
                            ColorAnimation { duration: 120 }
                        }
                    }
                    contentItem: Row {
                        anchors.fill: parent
                        anchors.margins: Common.Theme.spaceL
                        spacing: Common.Theme.spaceL

                        Rectangle {
                            width: 52
                            height: 52
                            radius: 18
                            color: overviewNavButton.checked ? Common.Theme.successSoft : "#edf2ff"
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "总"
                                color: overviewNavButton.checked ? Common.Theme.success : "#3f69d8"
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "系统总览"
                            color: overviewNavButton.checked ? "white" : Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    HoverHandler {
                        cursorShape: overviewNavButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }

                Button {
                    id: deviceNavButton
                    width: parent.width
                    height: 80
                    checkable: true
                    hoverEnabled: enabled
                    checked: root.navIndex === 1
                    transformOrigin: Item.Center
                    scale: deviceNavButton.enabled
                           ? (deviceNavButton.down ? 0.985 : (deviceNavButton.hovered ? 1.01 : 1.0))
                           : 1.0

                    Behavior on scale {
                        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
                    }

                    background: Rectangle {
                        radius: 24
                        color: deviceNavButton.checked
                               ? (deviceNavButton.down ? "#1f4ea9" : (deviceNavButton.hovered ? "#255bc2" : Common.Theme.accent))
                               : (deviceNavButton.down ? "#eef3fa" : (deviceNavButton.hovered ? "#f4f8ff" : "#fcfdff"))
                        border.width: 1
                        border.color: deviceNavButton.checked
                                      ? (deviceNavButton.hovered ? "#5b87ea" : Common.Theme.accent)
                                      : (deviceNavButton.hovered ? "#9fbaf0" : Common.Theme.borderColor)

                        Behavior on color {
                            ColorAnimation { duration: 120 }
                        }

                        Behavior on border.color {
                            ColorAnimation { duration: 120 }
                        }
                    }
                    contentItem: Row {
                        anchors.fill: parent
                        anchors.margins: Common.Theme.spaceL
                        spacing: Common.Theme.spaceL

                        Rectangle {
                            width: 52
                            height: 52
                            radius: 18
                            color: deviceNavButton.checked ? Common.Theme.successSoft : "#edf2ff"
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "设"
                                color: deviceNavButton.checked ? Common.Theme.success : "#3f69d8"
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "设备控制"
                            color: deviceNavButton.checked ? "white" : Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    HoverHandler {
                        cursorShape: deviceNavButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }

                Button {
                    id: alertNavButton
                    width: parent.width
                    height: 80
                    checkable: true
                    hoverEnabled: enabled
                    checked: root.navIndex === 2
                    transformOrigin: Item.Center
                    scale: alertNavButton.enabled
                           ? (alertNavButton.down ? 0.985 : (alertNavButton.hovered ? 1.01 : 1.0))
                           : 1.0

                    Behavior on scale {
                        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
                    }

                    background: Rectangle {
                        radius: 24
                        color: alertNavButton.checked
                               ? (alertNavButton.down ? "#1f4ea9" : (alertNavButton.hovered ? "#255bc2" : Common.Theme.accent))
                               : (alertNavButton.down ? "#eef3fa" : (alertNavButton.hovered ? "#f4f8ff" : "#fcfdff"))
                        border.width: 1
                        border.color: alertNavButton.checked
                                      ? (alertNavButton.hovered ? "#5b87ea" : Common.Theme.accent)
                                      : (alertNavButton.hovered ? "#9fbaf0" : Common.Theme.borderColor)

                        Behavior on color {
                            ColorAnimation { duration: 120 }
                        }

                        Behavior on border.color {
                            ColorAnimation { duration: 120 }
                        }
                    }
                    contentItem: Row {
                        anchors.fill: parent
                        anchors.margins: Common.Theme.spaceL
                        spacing: Common.Theme.spaceL

                        Rectangle {
                            width: 52
                            height: 52
                            radius: 18
                            color: alertNavButton.checked ? Common.Theme.successSoft : "#edf2ff"
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "警"
                                color: alertNavButton.checked ? Common.Theme.success : "#3f69d8"
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 20
                                font.bold: true
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "警告/报修"
                            color: alertNavButton.checked ? "white" : Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    HoverHandler {
                        cursorShape: alertNavButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

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

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                Text {
                                    text: root.pageTitle
                                    color: Common.Theme.textPrimary
                                    font.family: Common.Theme.fontFamily
                                    font.pixelSize: 34
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: root.pageDateText
                                    color: Common.Theme.textSecondary
                                    font.family: Common.Theme.fontFamily
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }
                            }

                            RowLayout {
                                spacing: Common.Theme.spaceS
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                                Common.StatusChip {
                                    text: root.locked ? "教室端已锁定" : "教室端可控"
                                    toneColor: root.locked ? Common.Theme.danger : Common.Theme.success
                                    fillColor: root.locked ? Common.Theme.dangerSoft : Common.Theme.successSoft
                                }

                                Common.StatusChip {
                                    visible: root.adminUsername !== ""
                                    text: root.isSuperAdmin ? ("超管 ") : ("管理员 ")
                                    toneColor: Common.Theme.accent
                                    fillColor: Common.Theme.accentSoft
                                }

                                Common.ActionButton {
                                    id: logoutButton
                                    visible: root.loggedIn
                                             && root.adminUsername !== ""
                                    text: "退出登录"
                                    variant: "secondary"
                                    minWidth: 96
                                }
                            }
                        }

                        RowLayout {
                            width: parent.width
                            spacing: Common.Theme.spaceS

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
                                id: manageAdminsButton
                                text: "管理管理员"
                                variant: "secondary"
                                visible: root.isSuperAdmin
                            }

                            Common.ActionButton {
                                id: changePasswordButton
                                text: "修改密码"
                                variant: "secondary"
                                visible: root.canChangePassword
                            }

                            Common.ActionButton {
                                id: syncButton
                                text: "同步数据"
                                variant: "secondary"
                            }

                            Common.ActionButton {
                                id: lockButton
                                text: root.locked ? "解除锁定" : "锁定教室端"
                                variant: root.locked ? "danger" : "primary"
                            }
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.navIndex

                    Flickable {
                        clip: true
                        contentWidth: width
                        contentHeight: overviewContent.height
                        ScrollBar.vertical: ScrollBar {}

                        Column {
                            id: overviewContent
                            width: parent.width
                            spacing: Common.Theme.spaceL

                            Flow {
                                width: parent.width
                                spacing: Common.Theme.spaceL

                                Common.MetricCard {
                                    width: overviewContent.width >= 1120 ? (overviewContent.width - Common.Theme.spaceL * 3) / 4 : (overviewContent.width - Common.Theme.spaceL) / 2
                                    title: "已接入教室"
                                    value: String(root.roomCount)
                                    iconText: "教"
                                    toneColor: Common.Theme.accent
                                    toneFill: Common.Theme.accentSoft
                                }

                                Common.MetricCard {
                                    width: overviewContent.width >= 1120 ? (overviewContent.width - Common.Theme.spaceL * 3) / 4 : (overviewContent.width - Common.Theme.spaceL) / 2
                                    title: "活跃教室"
                                    value: String(root.activeRoomCount)
                                    iconText: "活"
                                    toneColor: Common.Theme.success
                                    toneFill: Common.Theme.successSoft
                                }

                                Common.MetricCard {
                                    width: overviewContent.width >= 1120 ? (overviewContent.width - Common.Theme.spaceL * 3) / 4 : (overviewContent.width - Common.Theme.spaceL) / 2
                                    title: "在线设备"
                                    value: root.onlineDeviceCount + " / " + root.totalDeviceCount
                                    iconText: "设"
                                    toneColor: "#2d7abf"
                                    toneFill: "#e9f3ff"
                                }

                                Common.MetricCard {
                                    width: overviewContent.width >= 1120 ? (overviewContent.width - Common.Theme.spaceL * 3) / 4 : (overviewContent.width - Common.Theme.spaceL) / 2
                                    title: "待处理异常"
                                    value: String(root.alertCount)
                                    iconText: "警"
                                    toneColor: Common.Theme.danger
                                    toneFill: Common.Theme.dangerSoft
                                }
                            }

                            Flow {
                                width: parent.width
                                spacing: Common.Theme.spaceL

                                Common.SurfaceCard {
                                    width: overviewContent.width
                                           >= 1200 ? (overviewContent.width - Common.Theme.spaceL)
                                                     / 2 : overviewContent.width
                                    height: roomSection.implicitHeight + padding * 2

                                    Column {
                                        id: roomSection
                                        width: parent.width
                                        spacing: Common.Theme.spaceL

                                        Common.SectionHeader {
                                            width: parent.width
                                            title: "教室状态"
                                            subtitle: "按教室聚合查看运行与异常状态"
                                        }

                                        Flow {
                                            id: roomFlow
                                            width: parent.width
                                            spacing: Common.Theme.spaceM

                                            Repeater {
                                                model: root.roomSummaryListModel

                                                delegate: Rectangle {
                                                    required property string roomId
                                                    required property string stateText
                                                    required property color stateColor
                                                    required property color stateFill
                                                    required property string detail
                                                    width: roomFlow.width >= 840 ? (roomFlow.width - Common.Theme.spaceM) / 2 : roomFlow.width
                                                    height: 108
                                                    radius: Common.Theme.radiusM
                                                    color: "#fcfdff"
                                                    border.width: 1
                                                    border.color: Common.Theme.borderColor

                                                    Column {
                                                        anchors.fill: parent
                                                        anchors.margins: Common.Theme.spaceL
                                                        spacing: Common.Theme.spaceS

                                                        RowLayout {
                                                            width: parent.width

                                                            Text {
                                                                text: roomId
                                                                color: Common.Theme.textPrimary
                                                                font.family: Common.Theme.fontFamily
                                                                font.pixelSize: 22
                                                                font.bold: true
                                                            }

                                                            Item {
                                                                Layout.fillWidth: true
                                                            }

                                                            Common.StatusChip {
                                                                text: stateText
                                                                toneColor: stateColor
                                                                fillColor: stateFill
                                                            }
                                                        }

                                                        Text {
                                                            text: detail
                                                            color: Common.Theme.textSecondary
                                                            font.family: Common.Theme.fontFamily
                                                            font.pixelSize: 13
                                                            width: parent.width
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        Text {
                                            visible: !root.roomSummaryListModel
                                                     || root.roomSummaryListModel.count === 0
                                            text: "当前还没有已同步教室。"
                                            color: Common.Theme.textSecondary
                                            font.family: Common.Theme.fontFamily
                                            font.pixelSize: 13
                                        }
                                    }
                                }

                                Common.SurfaceCard {
                                    width: overviewContent.width
                                           >= 1200 ? (overviewContent.width - Common.Theme.spaceL)
                                                     / 2 : overviewContent.width
                                    height: alertSection.implicitHeight + padding * 2

                                    Column {
                                        id: alertSection
                                        width: parent.width
                                        spacing: Common.Theme.spaceL

                                        Common.SectionHeader {
                                            width: parent.width
                                            title: "速览"
                                            subtitle: "查看最新故障、离线与报修"

                                            Common.ActionButton {
                                                id: viewAllAlertsButton
                                                text: "查看全部"
                                                variant: "ghost"
                                                minWidth: 96
                                            }
                                        }

                                        Repeater {
                                            model: root.alertPreviewModel

                                            delegate: Rectangle {
                                                required property int alertId
                                                required property string level
                                                required property string roomId
                                                required property string deviceId
                                                required property string message
                                                width: parent.width
                                                height: 56
                                                radius: Common.Theme.radiusM
                                                color: "#fbfcff"
                                                border.width: 1
                                                border.color: Common.Theme.borderColor

                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: Common.Theme.spaceL
                                                    spacing: Common.Theme.spaceM

                                                    Rectangle {
                                                        width: 10
                                                        height: 10
                                                        radius: 5
                                                        color: Common.Theme.severityColor(
                                                                   level)
                                                        Layout.alignment: Qt.AlignVCenter
                                                    }

                                                    Common.StatusChip {
                                                        Layout.alignment: Qt.AlignVCenter
                                                        text: Common.Theme.severityLabel(
                                                                  level)
                                                        toneColor: Common.Theme.severityColor(
                                                                       level)
                                                        fillColor: Common.Theme.severityFill(
                                                                       level)
                                                    }

                                                    Text {
                                                        text: roomId + "/" + deviceId
                                                        color: Common.Theme.textPrimary
                                                        Layout.alignment: Qt.AlignVCenter
                                                        font.family: Common.Theme.fontFamily
                                                        font.pixelSize: 13
                                                        font.bold: true
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: message
                                                        color: Common.Theme.textSecondary
                                                        Layout.alignment: Qt.AlignVCenter
                                                        elide: Text.ElideRight
                                                        font.family: Common.Theme.fontFamily
                                                        font.pixelSize: 13
                                                    }

                                                    Common.ActionButton {
                                                        text: "已处理"
                                                        variant: "secondary"
                                                        minWidth: 76
                                                        maxWidth: 92
                                                        onClicked: root.processAlertRequested(
                                                                       alertId)
                                                    }
                                                }
                                            }
                                        }

                                        Text {
                                            visible: !root.alertPreviewModel
                                                     || root.alertPreviewModel.count === 0
                                            text: "当前没有新的异常。"
                                            color: Common.Theme.textSecondary
                                            font.family: Common.Theme.fontFamily
                                            font.pixelSize: 13
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Common.Theme.spaceL

                            StackLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                currentIndex: root.devicePageMode === "roomDevices" ? 1 : 0

                                Item {
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: Common.Theme.spaceL

                                        Common.SurfaceCard {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: roomHeaderContent.implicitHeight
                                                                    + padding * 2

                                            ColumnLayout {
                                                id: roomHeaderContent
                                                anchors.fill: parent
                                                anchors.margins: parent.padding
                                                spacing: Common.Theme.spaceM

                                                Common.SectionHeader {
                                                    Layout.fillWidth: true
                                                    title: "教室列表"
                                                    subtitle: "选择教室查看该教室内的全部设备"

                                                    Common.ActionButton {
                                                        text: "刷新设备"
                                                        variant: "secondary"
                                                        onClicked: root.refreshDevicePageRequested()
                                                    }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: root.statusText
                                                    color: Common.Theme.textSecondary
                                                    font.family: Common.Theme.fontFamily
                                                    font.pixelSize: 13
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }

                                        Common.SurfaceCard {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            padding: Common.Theme.spaceL

                                            Item {
                                                anchors.fill: parent

                                                ListView {
                                                    id: roomListView
                                                    anchors.fill: parent
                                                    clip: true
                                                    spacing: Common.Theme.spaceM
                                                    model: root.roomSummaryListModel

                                                    delegate: Rectangle {
                                                        required property string roomId
                                                        required property string stateText
                                                        required property color stateColor
                                                        required property color stateFill
                                                        required property string detail
                                                        property bool hovered: roomHover.hovered
                                                        property bool pressed: roomTap.pressed
                                                        width: ListView.view.width
                                                        height: roomCardContent.implicitHeight + Common.Theme.spaceL * 2
                                                        radius: Common.Theme.radiusL
                                                        color: root.selectedRoomId === roomId
                                                            ? (pressed ? "#e4edff" : (hovered ? "#edf4ff" : "#f3f7ff"))
                                                            : (pressed ? "#eef3fa" : (hovered ? "#f7faff" : "#fcfdff"))
                                                        border.width: 1
                                                        border.color: root.selectedRoomId === roomId
                                                            ? "#bdd0f4"
                                                            : (hovered ? "#d4e0f1" : Common.Theme.borderColor)
                                                        scale: pressed ? 0.992 : (hovered ? 1.006 : 1.0)

                                                        Behavior on color {
                                                            ColorAnimation { duration: 120 }
                                                        }

                                                        Behavior on border.color {
                                                            ColorAnimation { duration: 120 }
                                                        }

                                                        Behavior on scale {
                                                            NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
                                                        }

                                                        ColumnLayout {
                                                            id: roomCardContent
                                                            anchors.fill: parent
                                                            anchors.margins: Common.Theme.spaceL
                                                            spacing: Common.Theme.spaceS

                                                            RowLayout {
                                                                Layout.fillWidth: true
                                                                spacing: Common.Theme.spaceM

                                                                Text {
                                                                    text: roomId
                                                                    color: Common.Theme.textPrimary
                                                                    font.family: Common.Theme.fontFamily
                                                                    font.pixelSize: 22
                                                                    font.bold: true
                                                                }

                                                                Common.StatusChip {
                                                                    text: stateText
                                                                    toneColor: stateColor
                                                                    fillColor: stateFill
                                                                }

                                                                Item { Layout.fillWidth: true }

                                                                Common.ActionButton {
                                                                    text: "进入"
                                                                    variant: "secondary"
                                                                    minWidth: 88
                                                                    onClicked: root.roomSelected(roomId)
                                                                }
                                                            }

                                                            Text {
                                                                Layout.fillWidth: true
                                                                text: detail
                                                                color: Common.Theme.textSecondary
                                                                font.family: Common.Theme.fontFamily
                                                                font.pixelSize: 13
                                                                wrapMode: Text.WordWrap
                                                                maximumLineCount: 2
                                                                elide: Text.ElideRight
                                                            }
                                                        }

                                                        HoverHandler {
                                                            id: roomHover
                                                            cursorShape: Qt.PointingHandCursor
                                                        }

                                                        TapHandler {
                                                            id: roomTap
                                                            onTapped: root.roomSelected(roomId)
                                                        }
                                                    }
                                                }

                                                Text {
                                                    anchors.centerIn: parent
                                                    visible: !root.roomSummaryListModel || root.roomSummaryListModel.count === 0
                                                    text: "当前还没有可查看的教室。"
                                                    color: Common.Theme.textSecondary
                                                    font.family: Common.Theme.fontFamily
                                                    font.pixelSize: 13
                                                }
                                            }
                                        }
                                    }
                                }

                                Item {
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: Common.Theme.spaceL

                                        Common.SurfaceCard {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: roomDevicesHeaderContent.implicitHeight
                                                                    + padding * 2

                                            ColumnLayout {
                                                id: roomDevicesHeaderContent
                                                anchors.fill: parent
                                                anchors.margins: parent.padding
                                                spacing: Common.Theme.spaceM

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: Common.Theme.spaceM

                                                    Common.ActionButton {
                                                        text: "返回教室列表"
                                                        variant: "secondary"
                                                        minWidth: 126
                                                        onClicked: root.backToRoomsRequested()
                                                    }

                                                    ColumnLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 4

                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: root.selectedRoomId !== ""
                                                                  ? ("当前教室：" + root.selectedRoomId)
                                                                  : "当前教室"
                                                            color: Common.Theme.textPrimary
                                                            font.family: Common.Theme.fontFamily
                                                            font.pixelSize: 24
                                                            font.bold: true
                                                            elide: Text.ElideRight
                                                        }

                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: "查看并控制当前教室内的全部设备"
                                                            color: Common.Theme.textSecondary
                                                            font.family: Common.Theme.fontFamily
                                                            font.pixelSize: 13
                                                            elide: Text.ElideRight
                                                        }
                                                    }
                                                }

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: Common.Theme.spaceS

                                                    Common.ActionButton {
                                                        text: "开机"
                                                        enabled: root.hasSelectedDevice
                                                        onClicked: root.powerOnRequested()
                                                    }

                                                    Common.ActionButton {
                                                        text: "关机"
                                                        enabled: root.hasSelectedDevice
                                                        onClicked: root.powerOffRequested()
                                                    }

                                                    Common.ActionButton {
                                                        text: "刷新设备"
                                                        variant: "secondary"
                                                        onClicked: root.refreshDevicePageRequested()
                                                    }

                                                    Item { Layout.fillWidth: true }
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    text: root.statusText
                                                    color: Common.Theme.textSecondary
                                                    font.family: Common.Theme.fontFamily
                                                    font.pixelSize: 13
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }

                                        Common.SurfaceCard {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            padding: Common.Theme.spaceL

                                            Item {
                                                anchors.fill: parent

                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    spacing: Common.Theme.spaceM

                                                    Text {
                                                        text: "设备列表"
                                                        color: Common.Theme.textPrimary
                                                        font.family: Common.Theme.fontFamily
                                                        font.pixelSize: 20
                                                        font.bold: true
                                                    }

                                                    Item {
                                                        Layout.fillWidth: true
                                                        Layout.fillHeight: true

                                                        ListView {
                                                            id: deviceListView
                                                            anchors.fill: parent
                                                            clip: true
                                                            spacing: Common.Theme.spaceM
                                                            model: root.deviceListModel
                                                            delegate: root.deviceListDelegate
                                                        }

                                                        Text {
                                                            anchors.centerIn: parent
                                                            visible: !root.deviceListModel || root.deviceListModel.count === 0
                                                            text: "当前教室暂无设备。"
                                                            color: Common.Theme.textSecondary
                                                            font.family: Common.Theme.fontFamily
                                                            font.pixelSize: 13
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Common.Theme.spaceL

                            Common.SurfaceCard {
                                Layout.fillWidth: true
                                Layout.preferredHeight: alertHeaderContent.implicitHeight
                                                        + padding * 2

                                Column {
                                    id: alertHeaderContent
                                    width: parent.width
                                    spacing: Common.Theme.spaceM

                                    Common.SectionHeader {
                                        width: parent.width
                                        title: "警告/报修"

                                        Common.ActionButton {
                                            text: "全部处理"
                                            variant: "secondary"
                                            minWidth: 108
                                            onClicked: root.processAllAlertsRequested()
                                        }
                                    }

                                    Text {
                                        text: "当前共 " + root.alertCount + " 条异常记录"
                                        color: Common.Theme.textSecondary
                                        font.family: Common.Theme.fontFamily
                                        font.pixelSize: 13
                                    }
                                }
                            }

                            Common.SurfaceCard {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                padding: Common.Theme.spaceL

                                ListView {
                                    anchors.fill: parent
                                    clip: true
                                    spacing: Common.Theme.spaceM
                                    model: root.alertListModel

                                    delegate: Rectangle {
                                        required property int alertId
                                        required property string level
                                        required property string roomId
                                        required property string deviceId
                                        required property string createdAt
                                        required property string message
                                        width: ListView.view.width
                                        height: 84
                                        radius: Common.Theme.radiusM
                                        color: "#fcfdff"
                                        border.width: 1
                                        border.color: Common.Theme.borderColor

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: Common.Theme.spaceL
                                            spacing: Common.Theme.spaceS

                                            RowLayout {
                                                width: parent.width
                                                spacing: Common.Theme.spaceM

                                                Common.StatusChip {
                                                    text: Common.Theme.severityLabel(
                                                              level)
                                                    toneColor: Common.Theme.severityColor(
                                                                   level)
                                                    fillColor: Common.Theme.severityFill(
                                                                   level)
                                                }

                                                Text {
                                                    text: roomId + "/" + deviceId
                                                    color: Common.Theme.textPrimary
                                                    font.family: Common.Theme.fontFamily
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                }

                                                Item {
                                                    Layout.fillWidth: true
                                                }

                                                Text {
                                                    text: createdAt
                                                    color: Common.Theme.textSecondary
                                                    font.family: Common.Theme.fontFamily
                                                    font.pixelSize: 12
                                                }

                                                Common.ActionButton {
                                                    text: "已处理"
                                                    variant: "secondary"
                                                    minWidth: 76
                                                    maxWidth: 92
                                                    onClicked: root.processAlertRequested(
                                                                   alertId)
                                                }
                                            }

                                            Text {
                                                text: message
                                                color: Common.Theme.textSecondary
                                                width: parent.width
                                                wrapMode: Text.WordWrap
                                                maximumLineCount: 2
                                                elide: Text.ElideRight
                                                font.family: Common.Theme.fontFamily
                                                font.pixelSize: 13
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: !root.loggedIn
        z: 20

        Common.SurfaceCard {
            id: loginCard
            width: Math.min(440, root.width - Common.Theme.spaceXL * 2)
            height: Math.min(loginContent.implicitHeight + padding * 2,
                             root.height - Common.Theme.spaceXL * 2)
            anchors.centerIn: parent

            Flickable {
                anchors.fill: parent
                contentWidth: width
                contentHeight: loginContent.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                Column {
                    id: loginContent
                    width: loginCard.width - loginCard.padding * 2
                    spacing: Common.Theme.spaceL

                    Rectangle {
                        width: 56
                        height: 56
                        radius: 18
                        color: Common.Theme.accentSoft

                        Text {
                            anchors.centerIn: parent
                            text: "管"
                            color: Common.Theme.accent
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 22
                            font.bold: true
                        }
                    }

                    Text {
                        text: "管理员登录"
                        color: Common.Theme.textPrimary
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 28
                        font.bold: true
                    }

                    TextField {
                        id: userField
                        width: parent.width
                        height: 54
                        placeholderText: "用户名"
                        text: ""
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
                            border.color: userField.activeFocus ? Common.Theme.accent : "#cfd8e6"
                        }
                    }

                    TextField {
                        id: passField
                        width: parent.width
                        height: 54
                        placeholderText: "密码"
                        echoMode: TextInput.Password
                        text: ""
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
                            border.color: passField.activeFocus ? Common.Theme.accent : "#cfd8e6"
                        }
                    }

                    Text {
                        visible: root.statusText !== ""
                                 && root.statusText !== "系统待命"
                        width: parent.width
                        text: root.statusText
                        color: Common.Theme.textSecondary
                        wrapMode: Text.WordWrap
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 13
                    }

                    Item {
                        width: 1
                        height: 8
                    }

                    Common.ActionButton {
                        id: loginButton
                        anchors.right: parent.right
                        text: "进入系统"
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: root.busy
        color: "#3e0d1625"
        z: 30

        Column {
            anchors.centerIn: parent
            spacing: 10

            BusyIndicator {
                running: root.busy
                width: 48
                height: 48
            }

            Text {
                text: "正在提交请求..."
                color: "white"
                font.family: Common.Theme.fontFamily
                font.pixelSize: 14
            }
        }
    }
}
