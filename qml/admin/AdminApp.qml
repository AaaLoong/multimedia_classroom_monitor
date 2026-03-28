import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import "../common" as Common

ApplicationWindow {
    id: window
    visible: true
    width: 1440
    height: 960
    minimumWidth: 1280
    minimumHeight: 800
    title: "多媒体教室管理端"
    color: Common.Theme.pageBg

    Material.theme: Material.Light
    Material.primary: Common.Theme.accent
    Material.accent: Common.Theme.accent
    Material.background: Common.Theme.pageBg
    Material.foreground: Common.Theme.textPrimary

    property bool loggedIn: false
    property bool locked: api.locked
    property bool isSuperAdmin: api.superAdmin
    property bool canManageAdmins: api.canManageAdmins
    property bool canChangePassword: api.canChangePassword
    property bool manualRefresh: false
    property int navIndex: 0
    property int selectedDeviceIndex: -1
    property string statusText: "系统待命"
    property string adminUsername: api.adminUsername
    property var devicesCache: []

    ListModel { id: alertModel }
    ListModel { id: alertPreviewModel }
    ListModel { id: roomSummaryModel }
    ListModel { id: adminUserModel }

    function formatDateCN(d) {
        return d.getFullYear() + "年" + (d.getMonth() + 1) + "月" + d.getDate() + "日 星期" + ["日", "一", "二", "三", "四", "五", "六"][d.getDay()]
    }

    function formatTimestamp(ts) {
        if (!ts)
            return ""
        let s = String(ts).trim()
        const m = s.match(/^(\d{4}-\d{2}-\d{2})[T\s](\d{2}):(\d{2})/)
        if (m)
            return m[1] + " " + m[2] + ":" + m[3]
        const d = new Date(s)
        if (!isNaN(d.getTime())) {
            const p2 = function(n) { return n < 10 ? "0" + n : "" + n }
            return d.getFullYear() + "-" + p2(d.getMonth() + 1) + "-" + p2(d.getDate()) + " " + p2(d.getHours()) + ":" + p2(d.getMinutes())
        }
        return s
    }

    function uniqueRoomCount() {
        let map = {}
        for (let i = 0; i < devicesCache.length; ++i) {
            const rid = devicesCache[i].room_id ? devicesCache[i].room_id : devicesCache[i].roomId
            if (rid)
                map[rid] = true
        }
        return Object.keys(map).length
    }

    function activeRoomCount() {
        let map = {}
        for (let i = 0; i < devicesCache.length; ++i) {
            const d = devicesCache[i]
            const rid = d.room_id ? d.room_id : d.roomId
            if (!rid)
                continue
            if (d.status === "on" || d.status === "starting")
                map[rid] = true
        }
        return Object.keys(map).length
    }

    function onlineDeviceCount() {
        let count = 0
        for (let i = 0; i < devicesCache.length; ++i) {
            if (devicesCache[i].status !== "offline")
                count += 1
        }
        return count
    }

    function roomState(item) {
        if (item.fault > 0 || item.offline > 0)
            return "告警"
        if (item.on > 0)
            return "使用中"
        return "空闲中"
    }

    function roomStateColor(state) {
        if (state === "告警")
            return Common.Theme.danger
        if (state === "使用中")
            return Common.Theme.success
        return Common.Theme.textSecondary
    }

    function roomStateFill(state) {
        if (state === "告警")
            return Common.Theme.dangerSoft
        if (state === "使用中")
            return Common.Theme.successSoft
        return Common.Theme.neutralSoft
    }

    function buildRoomSummary() {
        roomSummaryModel.clear()
        let map = {}
        for (let i = 0; i < devicesCache.length; ++i) {
            const d = devicesCache[i]
            const rid = d.room_id ? d.room_id : d.roomId
            if (!rid)
                continue
            if (!map[rid])
                map[rid] = { roomId: rid, on: 0, off: 0, fault: 0, offline: 0 }
            if (d.status === "on")
                map[rid].on += 1
            else if (d.status === "fault")
                map[rid].fault += 1
            else if (d.status === "offline")
                map[rid].offline += 1
            else
                map[rid].off += 1
        }

        const keys = Object.keys(map).sort()
        for (let i = 0; i < keys.length; ++i) {
            const item = map[keys[i]]
            const state = roomState(item)
            roomSummaryModel.append({
                roomId: item.roomId,
                stateText: state,
                stateColor: roomStateColor(state),
                stateFill: roomStateFill(state),
                detail: "开机 " + item.on + " / 待机 " + item.off + " / 异常 " + (item.fault + item.offline)
            })
        }
    }

    function rebuildAlertModels(alerts) {
        alertModel.clear()
        alertPreviewModel.clear()
        for (let i = 0; i < alerts.length; ++i) {
            const a = alerts[i]
            const item = {
                level: a.level ? a.level : "warning",
                roomId: a.room_id ? a.room_id : "",
                deviceId: a.device_id ? a.device_id : "",
                message: a.message ? a.message : "",
                createdAt: a.created_at ? formatTimestamp(a.created_at) : ""
            }
            alertModel.append(item)
            if (i < 6)
                alertPreviewModel.append(item)
        }
    }

    function selectedDevice() {
        if (selectedDeviceIndex < 0 || selectedDeviceIndex >= deviceModel.rowCount())
            return null
        return deviceModel.get(selectedDeviceIndex)
    }

    function sendSelectedCommand(action) {
        const d = selectedDevice()
        if (!d || !d.deviceId)
            return
        statusText = action === "power_on" ? "开机指令已下发" : "关机指令已下发"
        api.sendCommand(d.roomId, d.deviceId, action)
    }

    function resetAdminUsers(admins) {
        adminUserModel.clear()
        for (let i = 0; i < admins.length; ++i) {
            const item = admins[i]
            adminUserModel.append({
                username: item.username ? item.username : "",
                isSuper: item.is_super ? true : false,
                canChangePassword: item.can_change_password ? true : false,
                createdAt: item.created_at ? formatTimestamp(item.created_at) : "",
                passwordUpdatedAt: item.password_updated_at ? formatTimestamp(item.password_updated_at) : ""
            })
        }
    }

    function scheduleDeviceRefresh() {
        if (!window.loggedIn)
            return
        adminDevicesEventTimer.stop()
        adminDevicesEventTimer.start()
    }

    function scheduleAlertRefresh() {
        if (!window.loggedIn)
            return
        adminAlertsEventTimer.stop()
        adminAlertsEventTimer.start()
    }

    Timer {
        id: adminDevicesEventTimer
        interval: 200
        repeat: false
        onTriggered: {
            if (window.loggedIn)
                api.fetchDevices()
        }
    }

    Timer {
        id: adminAlertsEventTimer
        interval: 200
        repeat: false
        onTriggered: {
            if (window.loggedIn)
                api.fetchAlerts()
        }
    }

    Timer {
        id: adminFallbackSyncTimer
        interval: 60000
        running: window.loggedIn
        repeat: true
        onTriggered: {
            api.fetchDevices()
            api.fetchAlerts()
            api.fetchLock()
        }
    }
    Component {
        id: adminDeviceDelegate

        Rectangle {
            required property int index
            required property string roomId
            required property string deviceId
            required property string name
            required property string status
            required property string updated
            id: deviceRow
            property bool hovered: deviceHover.hovered
            property bool pressed: deviceTap.pressed
            width: ListView.view.width
            height: 84
            radius: Common.Theme.radiusM
            color: window.selectedDeviceIndex === index
                ? (pressed ? "#e4edff" : (hovered ? "#edf4ff" : "#f3f7ff"))
                : (pressed ? "#eef3fa" : (hovered ? "#f7faff" : "#fcfdff"))
            border.width: 1
            border.color: window.selectedDeviceIndex === index
                ? "#bdd0f4"
                : (hovered ? "#d4e0f1" : Common.Theme.borderColor)
            scale: pressed ? 0.992 : 1.0

            Behavior on color {
                ColorAnimation { duration: 120 }
            }

            Behavior on border.color {
                ColorAnimation { duration: 120 }
            }

            Behavior on scale {
                NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
            }

            Column {
                anchors.fill: parent
                anchors.margins: Common.Theme.spaceL
                spacing: Common.Theme.spaceS

                RowLayout {
                    width: parent.width
                    spacing: Common.Theme.spaceM

                    Text {
                        text: roomId
                        color: Common.Theme.textPrimary
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Text {
                        text: deviceId
                        color: Common.Theme.textSecondary
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 13
                    }

                    Item { Layout.fillWidth: true }

                    Common.StatusChip {
                        text: Common.Theme.deviceStatusLabel(status)
                        toneColor: Common.Theme.deviceStatusColor(status)
                        fillColor: Common.Theme.deviceStatusFill(status)
                    }
                }

                RowLayout {
                    width: parent.width

                    Text {
                        text: name
                        color: Common.Theme.textPrimary
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: "更新时间 " + window.formatTimestamp(updated)
                        color: Common.Theme.textSecondary
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
            }

            HoverHandler {
                id: deviceHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                id: deviceTap
                onTapped: window.selectedDeviceIndex = index
            }
        }
    }

    Common.AppDialog {
        id: manageAdminsDialog
        dialogTitle: "管理管理员"
        preferredDialogWidth: 760
        preferredDialogHeight: 720
        minimumDialogWidth: 620
        minimumDialogHeight: 440

        Text {
            Layout.fillWidth: true
            text: "仅超级管理员可新增或删除普通管理员。初始超级管理员账号固定保留，不支持改密。"
            wrapMode: Text.WordWrap
            color: Common.Theme.textSecondary
            font.family: Common.Theme.fontFamily
            font.pixelSize: 13
        }

        Common.SurfaceCard {
            id: createAdminCard
            Layout.fillWidth: true
            implicitHeight: createAdminSection.implicitHeight + padding * 2
            clipContent: false
            fillColor: "#f7faff"

            ColumnLayout {
                id: createAdminSection
                anchors.fill: parent
                anchors.margins: createAdminCard.padding
                spacing: Common.Theme.spaceM

                Common.SectionHeader {
                    width: parent.width
                    title: "新增普通管理员"
                    subtitle: "新增账号默认是普通管理员，不会授予超管权限。"
                    maxTitleLines: 1
                    maxSubtitleLines: 2
                }

                TextField {
                    id: newAdminUserField
                    Layout.fillWidth: true
                    placeholderText: "账号，3 到 32 位，字母开头"
                }

                TextField {
                    id: newAdminPasswordField
                    Layout.fillWidth: true
                    placeholderText: "初始密码，6 到 64 位"
                    echoMode: TextInput.Password
                }

                Text {
                    Layout.fillWidth: true
                    text: "创建后可由普通管理员自行修改密码。"
                    color: Common.Theme.textSecondary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item { Layout.fillWidth: true }

                    Common.ActionButton {
                        text: "新增管理员"
                        enabled: newAdminUserField.text.trim() !== "" && newAdminPasswordField.text !== ""
                        onClicked: api.createAdminUser(newAdminUserField.text, newAdminPasswordField.text)
                    }
                }
            }
        }

        Common.SectionHeader {
            Layout.fillWidth: true
            title: "管理员列表"
            subtitle: "超级管理员固定保留，普通管理员支持删除。"
            maxTitleLines: 1
            maxSubtitleLines: 2
        }

        Column {
            Layout.fillWidth: true
            spacing: Common.Theme.spaceM

            Repeater {
                model: adminUserModel

                delegate: Common.SurfaceCard {
                    required property string username
                    required property bool isSuper
                    required property bool canChangePassword
                    required property string createdAt
                    required property string passwordUpdatedAt
                    width: parent.width
                    implicitHeight: adminCardContent.implicitHeight + padding * 2
                    clipContent: false

                    ColumnLayout {
                        id: adminCardContent
                        anchors.fill: parent
                        anchors.margins: parent.padding
                        spacing: Common.Theme.spaceS

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Common.Theme.spaceM

                            Text {
                                text: username
                                color: Common.Theme.textPrimary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Common.StatusChip {
                                text: isSuper ? "超级管理员" : "普通管理员"
                                toneColor: isSuper ? Common.Theme.warning : Common.Theme.accent
                                fillColor: isSuper ? "#fff4dd" : Common.Theme.accentSoft
                            }

                            Item { Layout.fillWidth: true }

                            Common.ActionButton {
                                visible: !isSuper
                                text: "删除"
                                variant: "danger"
                                onClicked: api.deleteAdminUser(username)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: isSuper
                                  ? "初始超级管理员固定保留，不支持删除和修改密码。"
                                  : ("创建时间 " + createdAt + " | 最近改密 " + (passwordUpdatedAt !== "" ? passwordUpdatedAt : "未记录"))
                            color: Common.Theme.textSecondary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: adminUserModel.count === 0
                text: "当前没有可展示的管理员。"
                color: Common.Theme.textSecondary
                font.family: Common.Theme.fontFamily
                font.pixelSize: 13
            }
        }

        onOpened: {
            if (window.canManageAdmins)
                api.fetchAdminUsers()
        }
    }

    Common.AppDialog {
        id: changePasswordDialog
        dialogTitle: "修改密码"
        preferredDialogWidth: 520
        preferredDialogHeight: 420
        minimumDialogWidth: 440
        minimumDialogHeight: 320

        Common.SurfaceCard {
            id: changePasswordCard
            Layout.fillWidth: true
            implicitHeight: changePasswordSection.implicitHeight + padding * 2
            clipContent: false
            fillColor: "#f7faff"

            ColumnLayout {
                id: changePasswordSection
                anchors.fill: parent
                anchors.margins: changePasswordCard.padding
                spacing: Common.Theme.spaceM

                Text {
                    Layout.fillWidth: true
                    text: "当前登录账号: " + window.adminUsername
                    color: Common.Theme.textSecondary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                TextField {
                    id: oldPasswordField
                    Layout.fillWidth: true
                    placeholderText: "当前密码"
                    echoMode: TextInput.Password
                }

                TextField {
                    id: newPasswordField
                    Layout.fillWidth: true
                    placeholderText: "新密码"
                    echoMode: TextInput.Password
                }

                TextField {
                    id: confirmPasswordField
                    Layout.fillWidth: true
                    placeholderText: "确认新密码"
                    echoMode: TextInput.Password
                }

                Text {
                    Layout.fillWidth: true
                    text: "普通管理员仅可修改本人密码。"
                    color: Common.Theme.textSecondary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item { Layout.fillWidth: true }

                    Common.ActionButton {
                        text: "确认修改"
                        enabled: oldPasswordField.text !== ""
                                 && newPasswordField.text !== ""
                                 && confirmPasswordField.text !== ""
                        onClicked: {
                            if (newPasswordField.text !== confirmPasswordField.text) {
                                window.statusText = "两次输入的新密码不一致"
                                return
                            }
                            api.changeMyPassword(oldPasswordField.text, newPasswordField.text)
                        }
                    }
                }
            }
        }
    }

    AdminAppForm {
        id: form
        anchors.fill: parent
        loggedIn: window.loggedIn
        locked: window.locked
        busy: api.busy
        isSuperAdmin: window.isSuperAdmin
        canChangePassword: window.canChangePassword
        navIndex: window.navIndex
        selectedDeviceIndex: window.selectedDeviceIndex
        hasSelectedDevice: window.selectedDevice() !== null
        pageTitle: window.navIndex === 0 ? "系统总览" : (window.navIndex === 1 ? "设备控制" : "告警中心")
        pageDateText: "多媒体教室管理系统 | " + window.formatDateCN(new Date())
        statusText: window.statusText
        adminUsername: window.adminUsername
        overviewHeadline: "统一感知教室接入、设备活跃度与告警分布"
        overviewFootnote: "当前共接入 " + window.uniqueRoomCount() + " 间教室，在线设备 " + window.onlineDeviceCount() + " 台。"
        roomCount: window.uniqueRoomCount()
        activeRoomCount: window.activeRoomCount()
        onlineDeviceCount: window.onlineDeviceCount()
        totalDeviceCount: window.devicesCache.length
        alertCount: alertModel.count
        roomSummaryListModel: roomSummaryModel
        alertPreviewModel: alertPreviewModel
        alertListModel: alertModel
        deviceListModel: deviceModel
        deviceListDelegate: adminDeviceDelegate
    }

    Connections {
        target: form.navOverviewButton
        function onClicked() { window.navIndex = 0 }
    }

    Connections {
        target: form.navDeviceButton
        function onClicked() { window.navIndex = 1 }
    }

    Connections {
        target: form.navAlertButton
        function onClicked() { window.navIndex = 2 }
    }

    Connections {
        target: form.syncButton
        function onClicked() {
            window.manualRefresh = true
            api.fetchDevices()
            api.fetchAlerts()
            api.fetchLock()
        }
    }

    Connections {
        target: form.lockButton
        function onClicked() { api.setLock(!window.locked) }
    }

    Connections {
        target: form.manageAdminsButton
        function onClicked() { manageAdminsDialog.open() }
    }

    Connections {
        target: form.changePasswordButton
        function onClicked() { changePasswordDialog.open() }
    }

    Connections {
        target: form.viewAllAlertsButton
        function onClicked() { window.navIndex = 2 }
    }

    Connections {
        target: form.powerOnButton
        function onClicked() { window.sendSelectedCommand("power_on") }
    }

    Connections {
        target: form.powerOffButton
        function onClicked() { window.sendSelectedCommand("power_off") }
    }

    Connections {
        target: form.refreshDevicesButton
        function onClicked() {
            window.manualRefresh = true
            api.fetchDevices()
        }
    }

    Connections {
        target: form.loginButton
        function onClicked() { api.login(form.userField.text, form.passField.text) }
    }

    Connections {
        target: api

        function onLoginSucceeded() {
            window.loggedIn = true
            window.statusText = "登录成功，正在同步设备状态"
            api.startAdminEventStream()
            api.fetchDevices()
            api.fetchLock()
            api.fetchAlerts()
            if (window.canManageAdmins)
                api.fetchAdminUsers()
        }

        function onLoginFailed(message) {
            window.loggedIn = false
            api.stopEventStream()
            adminUserModel.clear()
            window.statusText = message
        }

        function onUnauthorized(message) {
            window.loggedIn = false
            api.stopEventStream()
            window.devicesCache = []
            window.selectedDeviceIndex = -1
            deviceModel.setDevices([])
            alertModel.clear()
            alertPreviewModel.clear()
            roomSummaryModel.clear()
            adminUserModel.clear()
            window.statusText = message
        }

        function onDevicesReceived(devices) {
            window.devicesCache = devices ? devices : []
            deviceModel.setDevices(window.devicesCache)
            if (deviceModel.rowCount() > 0) {
                if (window.selectedDeviceIndex < 0 || window.selectedDeviceIndex >= deviceModel.rowCount())
                    window.selectedDeviceIndex = 0
            } else {
                window.selectedDeviceIndex = -1
            }
            if (window.manualRefresh)
                window.statusText = "设备与教室状态已更新"
            window.manualRefresh = false
            window.buildRoomSummary()
        }

        function onAlertsReceived(alerts) {
            window.rebuildAlertModels(alerts)
        }

        function onLockChanged(lockedValue) {
            window.locked = lockedValue
            window.statusText = lockedValue ? "教室端已被管理端锁定" : "教室端已解除锁定"
        }

        function onAdminUsersReceived(admins) {
            window.resetAdminUsers(admins ? admins : [])
        }

        function onServerDevicesChanged(roomId) {
            window.scheduleDeviceRefresh()
        }

        function onServerAlertsChanged(roomId, deviceId) {
            window.scheduleAlertRefresh()
        }

        function onServerRegistrationChanged(roomId, registered) {
            window.scheduleDeviceRefresh()
        }

        function onOperationFinished(action, message) {
            if (action === "admin_create") {
                newAdminUserField.text = ""
                newAdminPasswordField.text = ""
            } else if (action === "password_change") {
                oldPasswordField.text = ""
                newPasswordField.text = ""
                confirmPasswordField.text = ""
                changePasswordDialog.close()
            }
            window.statusText = message
        }
        function onError(message) { window.statusText = message }
    }
}
