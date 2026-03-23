import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../common" as Common

Item {
    id: root

    property bool loggedIn: false
    property bool locked: false
    property bool busy: false
    property int navIndex: 0
    property int selectedDeviceIndex: -1
    property bool hasSelectedDevice: false
    property string pageTitle: "系统总览"
    property string pageDateText: ""
    property string statusText: ""
    property string overviewHeadline: ""
    property string overviewFootnote: ""
    property int roomCount: 0
    property int activeRoomCount: 0
    property int onlineDeviceCount: 0
    property int totalDeviceCount: 0
    property int alertCount: 0
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
    property alias viewAllAlertsButton: viewAllAlertsButton
    property alias powerOnButton: powerOnButton
    property alias powerOffButton: powerOffButton
    property alias refreshDevicesButton: refreshDevicesButton
    property alias loginButton: loginButton
    property alias userField: userField
    property alias passField: passField
    property alias deviceListView: deviceListView

    readonly property int sidebarWidth: 276
    readonly property int pageGap: Common.Theme.spaceL

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#f7f9fc" }
            GradientStop { position: 0.5; color: Common.Theme.pageBg }
            GradientStop { position: 1.0; color: "#f8fafc" }
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

                            Text {
                                text: "Campus Control"
                                color: Common.Theme.textSecondary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 13
                            }
                        }
                    }
                }

                Button {
                    id: overviewNavButton
                    width: parent.width
                    height: 80
                    checkable: true
                    checked: root.navIndex === 0
                    background: Rectangle {
                        radius: 24
                        color: overviewNavButton.checked ? Common.Theme.accent : "#fcfdff"
                        border.width: 1
                        border.color: overviewNavButton.checked ? Common.Theme.accent : Common.Theme.borderColor
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
                            text: "总览"
                            color: overviewNavButton.checked ? "white" : Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }
                }

                Button {
                    id: deviceNavButton
                    width: parent.width
                    height: 80
                    checkable: true
                    checked: root.navIndex === 1
                    background: Rectangle {
                        radius: 24
                        color: deviceNavButton.checked ? Common.Theme.accent : "#fcfdff"
                        border.width: 1
                        border.color: deviceNavButton.checked ? Common.Theme.accent : Common.Theme.borderColor
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
                }

                Button {
                    id: alertNavButton
                    width: parent.width
                    height: 80
                    checkable: true
                    checked: root.navIndex === 2
                    background: Rectangle {
                        radius: 24
                        color: alertNavButton.checked ? Common.Theme.accent : "#fcfdff"
                        border.width: 1
                        border.color: alertNavButton.checked ? Common.Theme.accent : Common.Theme.borderColor
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
                            text: "告警中心"
                            color: alertNavButton.checked ? "white" : Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }
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

                            Common.StatusChip {
                                text: root.locked ? "教室端已锁定" : "教室端可控"
                                toneColor: root.locked ? Common.Theme.danger : Common.Theme.success
                                fillColor: root.locked ? Common.Theme.dangerSoft : Common.Theme.successSoft
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

                            Common.SurfaceCard {
                                width: parent.width
                                height: 176
                                fillColor: Common.Theme.inkDark
                                outlineColor: "#1d3454"

                                Column {
                                    anchors.fill: parent
                                    spacing: Common.Theme.spaceM

                                    Text {
                                        text: "总控中心"
                                        color: Common.Theme.inkDarkSoft
                                        font.family: Common.Theme.fontFamily
                                        font.pixelSize: 14
                                    }

                                    Text {
                                        text: root.overviewHeadline
                                        color: "white"
                                        width: parent.width
                                        wrapMode: Text.WordWrap
                                        font.family: Common.Theme.fontFamily
                                        font.pixelSize: 34
                                        font.bold: true
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: root.overviewFootnote
                                        color: "#a8bbd5"
                                        font.family: Common.Theme.fontFamily
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                    }

                                    Row {
                                        spacing: Common.Theme.spaceM

                                        Common.StatusChip {
                                            text: "活跃教室 " + root.activeRoomCount
                                            toneColor: Common.Theme.success
                                            fillColor: "#1f8f6a33"
                                        }

                                        Common.StatusChip {
                                            text: "告警事件 " + root.alertCount
                                            toneColor: Common.Theme.warning
                                            fillColor: "#b7791f33"
                                        }
                                    }
                                }
                            }

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
                                    title: "待处理告警"
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
                                    width: overviewContent.width >= 1200 ? (overviewContent.width - Common.Theme.spaceL) / 2 : overviewContent.width
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

                                                            Item { Layout.fillWidth: true }

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
                                            visible: !root.roomSummaryListModel || root.roomSummaryListModel.count === 0
                                            text: "当前还没有已同步教室。"
                                            color: Common.Theme.textSecondary
                                            font.family: Common.Theme.fontFamily
                                            font.pixelSize: 13
                                        }
                                    }
                                }

                                Common.SurfaceCard {
                                    width: overviewContent.width >= 1200 ? (overviewContent.width - Common.Theme.spaceL) / 2 : overviewContent.width
                                    height: alertSection.implicitHeight + padding * 2

                                    Column {
                                        id: alertSection
                                        width: parent.width
                                        spacing: Common.Theme.spaceL

                                        Common.SectionHeader {
                                            width: parent.width
                                            title: "告警速览"
                                            subtitle: "集中查看最新故障、离线与报修"

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

                                                Row {
                                                    anchors.fill: parent
                                                    anchors.margins: Common.Theme.spaceL
                                                    spacing: Common.Theme.spaceM

                                                    Rectangle {
                                                        width: 10
                                                        height: 10
                                                        radius: 5
                                                        color: Common.Theme.severityColor(level)
                                                        anchors.verticalCenter: parent.verticalCenter
                                                    }

                                                    Common.StatusChip {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: Common.Theme.severityLabel(level)
                                                        toneColor: Common.Theme.severityColor(level)
                                                        fillColor: Common.Theme.severityFill(level)
                                                    }

                                                    Text {
                                                        text: roomId + "/" + deviceId
                                                        color: Common.Theme.textPrimary
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        font.family: Common.Theme.fontFamily
                                                        font.pixelSize: 13
                                                        font.bold: true
                                                    }

                                                    Text {
                                                        text: message
                                                        color: Common.Theme.textSecondary
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        width: parent.width - 330
                                                        elide: Text.ElideRight
                                                        font.family: Common.Theme.fontFamily
                                                        font.pixelSize: 13
                                                    }
                                                }
                                            }
                                        }

                                        Text {
                                            visible: !root.alertPreviewModel || root.alertPreviewModel.count === 0
                                            text: "当前没有新的异常告警。"
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

                            Common.SurfaceCard {
                                Layout.fillWidth: true
                                Layout.preferredHeight: deviceHeaderContent.implicitHeight + padding * 2

                                Column {
                                    id: deviceHeaderContent
                                    width: parent.width
                                    spacing: Common.Theme.spaceM

                                    Common.SectionHeader {
                                        width: parent.width
                                        title: "设备控制面板"
                                    }

                                    RowLayout {
                                        width: parent.width
                                        spacing: Common.Theme.spaceM

                                        Common.ActionButton {
                                            id: powerOnButton
                                            text: "开机"
                                            enabled: root.hasSelectedDevice
                                        }

                                        Common.ActionButton {
                                            id: powerOffButton
                                            text: "关机"
                                            enabled: root.hasSelectedDevice
                                        }

                                        Common.ActionButton {
                                            id: refreshDevicesButton
                                            text: "刷新设备"
                                            variant: "secondary"
                                        }

                                        Item { Layout.fillWidth: true }

                                        Text {
                                            text: root.statusText
                                            color: Common.Theme.textSecondary
                                            font.family: Common.Theme.fontFamily
                                            font.pixelSize: 13
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            Common.SurfaceCard {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                padding: Common.Theme.spaceL

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

                                    ListView {
                                        id: deviceListView
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        spacing: Common.Theme.spaceM
                                        model: root.deviceListModel
                                        delegate: root.deviceListDelegate
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
                                Layout.preferredHeight: alertHeaderContent.implicitHeight + padding * 2

                                Column {
                                    id: alertHeaderContent
                                    width: parent.width
                                    spacing: Common.Theme.spaceM

                                    Common.SectionHeader {
                                        width: parent.width
                                        title: "告警中心"
                                    }

                                    Text {
                                        text: "当前共 " + root.alertCount + " 条告警记录"
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
                                                    text: Common.Theme.severityLabel(level)
                                                    toneColor: Common.Theme.severityColor(level)
                                                    fillColor: Common.Theme.severityFill(level)
                                                }

                                                Text {
                                                    text: roomId + "/" + deviceId
                                                    color: Common.Theme.textPrimary
                                                    font.family: Common.Theme.fontFamily
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                }

                                                Item { Layout.fillWidth: true }

                                                Text {
                                                    text: createdAt
                                                    color: Common.Theme.textSecondary
                                                    font.family: Common.Theme.fontFamily
                                                    font.pixelSize: 12
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

    Rectangle {
        anchors.fill: parent
        visible: !root.loggedIn
        color: "#7a101a2b"
        z: 20

        Common.SurfaceCard {
            id: loginCard
            width: Math.min(420, root.width - Common.Theme.spaceXL * 2)
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

                    Text {
                        text: "登录后进入管理后台"
                        color: Common.Theme.textSecondary
                        width: parent.width
                        wrapMode: Text.WordWrap
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 13
                    }

                    TextField {
                        id: userField
                        width: parent.width
                        placeholderText: "用户名"
                        text: ""
                    }

                    TextField {
                        id: passField
                        width: parent.width
                        placeholderText: "密码"
                        echoMode: TextInput.Password
                        text: ""
                    }

                    Item { width: 1; height: 8 }

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
        z: 19

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
