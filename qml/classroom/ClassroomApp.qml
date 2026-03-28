import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import "../common" as Common

ApplicationWindow {
    id: window
    visible: true
    width: 1220
    height: 820
    minimumWidth: 1220
    minimumHeight: 780
    title: "教室端控制台"
    color: Common.Theme.pageBg

    Material.theme: Material.Light
    Material.primary: Common.Theme.accent
    Material.accent: Common.Theme.accent
    Material.background: Common.Theme.pageBg
    Material.foreground: Common.Theme.textPrimary

    property string roomId: ""
    property string selectedBuilding: "A"
    property string selectedFloor: "1"
    property string selectedRoomSuffix: "01"
    property string inputRoomId: selectedBuilding + selectedFloor + selectedRoomSuffix
    property bool locked: api.locked
    property bool manualRefresh: false
    property bool backendReady: false
    property bool registered: api.registered
    property string statusText: "请选择教室号完成接入"
    property string pendingBulkAction: ""
    property bool bulkControlLocked: false
    property double bulkControlUnlockAtMs: 0
    property int syncedDeviceCount: 0
    property int syncedOnlineCount: 0
    property int syncedActiveCount: 0
    property int syncedFaultCount: 0
    property var pendingReportDevice: ({})
    property var buildingOptions: ["A", "B", "C", "D"]
    property var floorOptions: ["1", "2", "3", "4", "5", "6", "7"]
    property var roomSuffixOptions: ["01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12"]
    property var defaultDevices: [
        { "room_id": roomId, "device_id": "proj", "name": "投影仪", "status": "off" },
        { "room_id": roomId, "device_id": "amp", "name": "功放", "status": "off" },
        { "room_id": roomId, "device_id": "pc", "name": "讲台电脑", "status": "off" },
        { "room_id": roomId, "device_id": "mic", "name": "无线麦克风", "status": "off" },
        { "room_id": roomId, "device_id": "screen", "name": "电动幕布", "status": "off" }
    ]

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

    function validateRoomId(rid) {
        return /^([A-Da-d])([1-7])(0[1-9]|1[0-2])$/.test(String(rid).trim())
    }

    function applyRoomIdSelection(rid) {
        const m = String(rid).trim().match(/^([A-Da-d])([1-7])(0[1-9]|1[0-2])$/)
        if (!m)
            return false
        selectedBuilding = m[1].toUpperCase()
        selectedFloor = m[2]
        selectedRoomSuffix = m[3]
        return true
    }

    function localizedDeviceName(deviceId) {
        const map = {
            "proj": "投影仪",
            "amp": "功放",
            "pc": "讲台电脑",
            "mic": "无线麦克风",
            "screen": "电动幕布"
        }
        return map[deviceId] ? map[deviceId] : String(deviceId ? deviceId : "")
    }

    function chineseTitle(text) {
        const raw = String(text ? text : "").trim()
        if (!raw)
            return ""
        const matched = raw.match(/[\u4e00-\u9fff][\u4e00-\u9fff0-9A-Za-z（）()\-]*/g)
        if (matched && matched.length > 0)
            return matched.join(" ").trim()
        const stripped = raw.replace(/^[A-Za-z0-9_-]+\s*/, "").trim()
        return stripped ? stripped : raw
    }

    function displayDeviceTitle(name, deviceId) {
        const preferred = chineseTitle(name)
        if (preferred)
            return preferred
        return chineseTitle(localizedDeviceName(deviceId))
    }

    function updateDeviceStats(devices) {
        let total = 0
        let online = 0
        let active = 0
        let fault = 0

        for (let i = 0; i < devices.length; ++i) {
            const d = devices[i]
            const st = String(d.status ? d.status : "").toLowerCase()
            total += 1
            if (st !== "offline")
                online += 1
            if (st === "on" || st === "starting")
                active += 1
            if (st === "fault" || st === "offline")
                fault += 1
        }

        window.syncedDeviceCount = total
        window.syncedOnlineCount = online
        window.syncedActiveCount = active
        window.syncedFaultCount = fault
    }

    function resetDeviceStats() {
        window.syncedDeviceCount = 0
        window.syncedOnlineCount = 0
        window.syncedActiveCount = 0
        window.syncedFaultCount = 0
    }

    function currentRoomIdOrError() {
        const rid = window.inputRoomId.trim()
        if (!rid) {
            window.statusText = "请先选择教室号"
            return ""
        }
        if (!window.validateRoomId(rid)) {
            window.statusText = "教室号不在允许范围内"
            return ""
        }
        return rid
    }

    function randomFallbackPollInterval() {
        return 25000 + Math.floor(Math.random() * 10001)
    }

    function bulkLockRemainingSeconds() {
        if (!bulkControlLockTimer.running)
            return 0
        return Math.max(1, Math.ceil((window.bulkControlUnlockAtMs - Date.now()) / 1000))
    }

    function beginBulkControlLock(action) {
        window.bulkControlLocked = true
        window.bulkControlUnlockAtMs = Date.now() + 5000
        bulkControlLockTimer.restart()
        window.statusText = action === "power_on"
            ? "一键上课已触发，5 秒内禁止再次执行批量控制"
            : "一键下课已触发，5 秒内禁止再次执行批量控制"
    }

    function ensureBulkControlUnlocked(actionLabel) {
        if (!window.bulkControlLocked)
            return true
        window.statusText = actionLabel + "暂不可用，请在 "
            + window.bulkLockRemainingSeconds() + " 秒后重试"
        return false
    }

    function requestSingleDeviceCommand(deviceId, action) {
        if (!window.registered) {
            window.statusText = "教室未注册，无法执行设备控制"
            return
        }
        if (window.locked) {
            window.statusText = "当前已被管理端锁定，无法执行设备控制"
            return
        }
        if (window.bulkControlLocked) {
            window.statusText = "批量控制互锁中，5 秒内不响应单设备开关机"
            return
        }
        api.sendCommand(window.roomId, deviceId, action)
    }

    function scheduleScopedDeviceRefresh() {
        if (!window.registered || !window.roomId)
            return
        classroomEventRefreshTimer.stop()
        classroomEventRefreshTimer.start()
    }

    function rescheduleFallbackPolling() {
        classroomFallbackDeviceTimer.stop()
        if (!window.registered || !window.roomId)
            return
        classroomFallbackDeviceTimer.interval = window.randomFallbackPollInterval()
        classroomFallbackDeviceTimer.start()
    }

    function executeBulkAction(action) {
        if (!window.registered) {
            window.statusText = "教室未注册，无法执行"
            return
        }
        if (window.locked) {
            window.statusText = "当前已被管理端锁定，无法执行批量控制"
            return
        }
        if (!window.roomId) {
            window.statusText = "当前教室号为空，无法执行"
            return
        }

        let sendCount = 0
        let skippedOffline = 0
        for (let i = 0; i < deviceModel.rowCount(); ++i) {
            const d = deviceModel.get(i)
            if (!d || !d.deviceId)
                continue
            const rid = d.roomId ? d.roomId : (d.room_id ? d.room_id : window.roomId)
            if (rid !== window.roomId)
                continue

            const st = String(d.status ? d.status : "").toLowerCase()
            if (st === "offline") {
                skippedOffline += 1
                continue
            }
            if (action === "power_on" && (st === "on" || st === "starting"))
                continue
            if (action === "power_off" && st === "off")
                continue

            api.sendCommand(rid, d.deviceId, action)
            sendCount += 1
        }

        if (sendCount === 0) {
            if (skippedOffline > 0) {
                window.statusText = action === "power_on"
                    ? "没有可执行上课开机的设备，离线设备已跳过"
                    : "没有可执行下课关机的设备，离线设备已跳过"
            } else {
                window.statusText = action === "power_on"
                    ? "无需执行上课开机，设备已在线"
                    : "无需执行下课关机，设备已关闭"
            }
            return
        }

        window.statusText = (action === "power_on" ? "一键上课已下发 " : "一键下课已下发 ")
            + sendCount + " 台设备"
            + (skippedOffline > 0 ? ("，跳过 " + skippedOffline + " 台离线设备") : "")
    }

    function requestBulkAction(action) {
        if (!window.registered) {
            window.statusText = "教室未注册，无法执行"
            return
        }
        if (window.locked) {
            window.statusText = "当前已被管理端锁定，无法执行批量控制"
            return
        }
        if (!window.roomId) {
            window.statusText = "当前教室号为空，无法执行"
            return
        }
        if (!window.ensureBulkControlUnlocked(action === "power_on" ? "一键上课" : "一键下课"))
            return

        window.beginBulkControlLock(action)
        window.pendingBulkAction = action
        window.manualRefresh = false
        window.statusText = action === "power_on"
            ? "正在同步设备状态并执行一键上课，批量控制已锁定 5 秒..."
            : "正在同步设备状态并执行一键下课，批量控制已锁定 5 秒..."
        api.fetchDevicesByRoom(window.roomId)
    }

    Component.onCompleted: {
        if (!applyRoomIdSelection((typeof initialRoomId !== "undefined" && initialRoomId) ? initialRoomId : "A101"))
            applyRoomIdSelection("A101")
        deviceModel.setDevices(defaultDevices)
        api.fetchLock()
    }

    Timer {
        id: classroomEventRefreshTimer
        interval: 200
        repeat: false
        onTriggered: {
            if (window.registered && window.roomId)
                api.fetchDevicesByRoom(window.roomId)
        }
    }

    Timer {
        id: classroomFallbackDeviceTimer
        interval: 30000
        repeat: false
        onTriggered: {
            if (window.registered && window.roomId)
                api.fetchDevicesByRoom(window.roomId)
            window.rescheduleFallbackPolling()
        }
    }

    Timer {
        id: bulkControlLockTimer
        interval: 5000
        repeat: false
        onTriggered: {
            window.bulkControlLocked = false
            window.bulkControlUnlockAtMs = 0
            if (window.statusText.indexOf("批量控制已锁定 5 秒") >= 0
                    || window.statusText.indexOf("批量控制互锁中") >= 0)
                window.statusText = "批量控制锁已解除，可继续操作"
        }
    }

    Dialog {
        id: reportDialog
        modal: true
        width: 380
        height: 230
        x: (window.width - width) / 2
        y: (window.height - height) / 2
        standardButtons: Dialog.NoButton

        background: Common.SurfaceCard {
            padding: Common.Theme.spaceL
        }

        contentItem: ColumnLayout {
            anchors.fill: parent
            spacing: Common.Theme.spaceM

            Text {
                text: "设备报修"
                color: Common.Theme.textPrimary
                font.family: Common.Theme.fontFamily
                font.pixelSize: 20
                font.bold: true
            }

            Text {
                text: window.displayDeviceTitle(window.pendingReportDevice.name, window.pendingReportDevice.deviceId)
                color: Common.Theme.textSecondary
                font.family: Common.Theme.fontFamily
                font.pixelSize: 13
            }

            TextArea {
                id: reportInput
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: "请描述设备问题..."
                wrapMode: TextEdit.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                Common.ActionButton {
                    text: "取消"
                    variant: "secondary"
                    minWidth: 80
                    onClicked: reportDialog.close()
                }

                Common.ActionButton {
                    text: "提交"
                    minWidth: 80
                    onClicked: {
                        const msg = reportInput.text.trim()
                        if (!msg) {
                            window.statusText = "请先输入问题描述"
                            return
                        }
                        if (!window.pendingReportDevice || !window.pendingReportDevice.deviceId) {
                            window.statusText = "未选中设备"
                            return
                        }
                        window.statusText = "正在提交报修..."
                        api.submitReport(window.pendingReportDevice.roomId, window.pendingReportDevice.deviceId, msg)
                        reportDialog.close()
                    }
                }
            }
        }
    }

    Component {
        id: classroomDeviceDelegate

        Rectangle {
            required property string deviceId
            required property string name
            required property string status
            required property string updated
            width: ListView.view.width
            height: deviceContent.implicitHeight + Common.Theme.spaceL * 2
            radius: Common.Theme.radiusM
            color: "#fcfdff"
            border.width: 1
            border.color: Common.Theme.borderColor

            Column {
                id: deviceContent
                anchors.fill: parent
                anchors.margins: Common.Theme.spaceL
                spacing: Common.Theme.spaceS

                RowLayout {
                    width: parent.width
                    spacing: Common.Theme.spaceM

                    Text {
                        Layout.fillWidth: true
                        text: window.displayDeviceTitle(name, deviceId)
                        color: Common.Theme.textPrimary
                        font.family: Common.Theme.fontFamily
                        font.pixelSize: 18
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Common.StatusChip {
                        text: Common.Theme.deviceStatusLabel(status)
                        toneColor: Common.Theme.deviceStatusColor(status)
                        fillColor: Common.Theme.deviceStatusFill(status)
                    }
                }

                Text {
                    width: parent.width
                    text: "最近上报 " + window.formatTimestamp(updated)
                    color: Common.Theme.textSecondary
                    font.family: Common.Theme.fontFamily
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Flow {
                    width: parent.width
                    spacing: Common.Theme.spaceM

                    Common.ActionButton {
                        text: "报修"
                        variant: "secondary"
                        minWidth: 80
                        enabled: !window.locked && window.registered
                        onClicked: {
                            window.pendingReportDevice = { roomId: window.roomId, deviceId: deviceId, name: name }
                            reportInput.text = ""
                            reportDialog.open()
                        }
                    }

                    Common.ActionButton {
                        text: "关机"
                        variant: "secondary"
                        minWidth: 80
                        enabled: !window.locked && !window.bulkControlLocked && window.registered
                        onClicked: window.requestSingleDeviceCommand(deviceId, "power_off")
                    }

                    Common.ActionButton {
                        text: "开机"
                        minWidth: 80
                        enabled: !window.locked && !window.bulkControlLocked && window.registered
                        onClicked: window.requestSingleDeviceCommand(deviceId, "power_on")
                    }
                }
            }
        }
    }

    ClassroomAppForm {
        id: form
        anchors.fill: parent
        locked: window.locked
        busy: api.busy
        bulkControlLocked: window.bulkControlLocked
        registered: window.registered
        backendReady: window.backendReady
        roomId: window.roomId
        headerTitle: window.registered ? ("教室 " + window.roomId + " 控制台") : "教室接入控制台"
        inputRoomId: window.inputRoomId
        statusText: window.statusText
        deviceCount: window.syncedDeviceCount
        onlineCount: window.syncedOnlineCount
        activeCount: window.syncedActiveCount
        faultCount: window.syncedFaultCount
        buildingIndex: Math.max(0, window.buildingOptions.indexOf(window.selectedBuilding))
        floorIndex: Math.max(0, window.floorOptions.indexOf(window.selectedFloor))
        roomSuffixIndex: Math.max(0, window.roomSuffixOptions.indexOf(window.selectedRoomSuffix))
        buildingOptions: window.buildingOptions
        floorOptions: window.floorOptions
        roomSuffixOptions: window.roomSuffixOptions
        deviceListModel: deviceModel
        deviceDelegate: classroomDeviceDelegate
    }

    Connections {
        target: form.refreshButton
        function onClicked() {
            if (!window.registered) {
                window.statusText = "教室未注册，无法刷新设备"
                return
            }
            window.manualRefresh = true
            api.fetchDevicesByRoom(window.roomId)
        }
    }

    Connections {
        target: form.powerOnAllButton
        function onClicked() {
            window.requestBulkAction("power_on")
        }
    }

    Connections {
        target: form.powerOffAllButton
        function onClicked() {
            window.requestBulkAction("power_off")
        }
    }

    Connections {
        target: form.heartbeatButton
        function onClicked() { api.sendHeartbeat(window.roomId) }
    }

    Connections {
        target: form.buildingCombo
        function onActivated() { window.selectedBuilding = form.buildingCombo.currentText }
    }

    Connections {
        target: form.floorCombo
        function onActivated() { window.selectedFloor = form.floorCombo.currentText }
    }

    Connections {
        target: form.suffixCombo
        function onActivated() { window.selectedRoomSuffix = form.suffixCombo.currentText }
    }

    Connections {
        target: form.checkStatusButton
        function onClicked() {
            const rid = window.currentRoomIdOrError()
            if (!rid)
                return
            api.fetchRegistration(rid)
        }
    }

    Connections {
        target: form.registerButton
        function onClicked() {
            const rid = window.currentRoomIdOrError()
            if (!rid)
                return
            api.registerClassroom(rid)
        }
    }

    Connections {
        target: api

        function onDevicesReceived(devices) {
            if (!window.registered) {
                window.backendReady = false
                window.resetDeviceStats()
                return
            }

            window.rescheduleFallbackPolling()

            let scoped = []
            for (let i = 0; i < devices.length; ++i) {
                const d = devices[i]
                const rid = d.room_id ? d.room_id : d.roomId
                if (rid === window.roomId)
                    scoped.push(d)
            }

            let displayDevices = scoped
            if (scoped.length === 0 && deviceModel.rowCount() === 0 && !window.locked)
                displayDevices = window.defaultDevices

            deviceModel.setDevices(displayDevices)
            window.updateDeviceStats(displayDevices)

            const pendingAction = window.pendingBulkAction
            window.pendingBulkAction = ""

            if (window.manualRefresh && !pendingAction)
                window.statusText = "设备状态已更新"
            else if (!window.backendReady && !pendingAction)
                window.statusText = "教室已接入，设备同步完成"

            window.backendReady = true
            window.manualRefresh = false

            if (pendingAction)
                window.executeBulkAction(pendingAction)
        }

        function onRegistrationChanged(v) {
            window.registered = v
            if (v) {
                window.roomId = window.inputRoomId.trim()
                window.statusText = "注册成功，正在同步设备..."
                window.manualRefresh = false
                api.startClassroomEventStream(window.roomId)
                window.rescheduleFallbackPolling()
                api.fetchDevicesByRoom(window.roomId)
            } else {
                window.statusText = "教室未注册，请先完成接入"
                window.backendReady = false
                window.roomId = ""
                api.stopEventStream()
                classroomFallbackDeviceTimer.stop()
                classroomEventRefreshTimer.stop()
                deviceModel.refresh()
                window.resetDeviceStats()
            }
        }

        function onRegistrationCheckedChanged(checked) {
            if (checked && !window.registered) {
                window.statusText = "当前教室尚未注册，可立即发起接入"
                window.backendReady = false
            } else if (checked && window.registered) {
                window.statusText = "教室已接入，正在同步设备..."
            }
        }

        function onLockChanged(lockedValue) {
            window.locked = lockedValue
            if (lockedValue)
                window.statusText = "已被管理端锁定"
        }

        function onServerDevicesChanged(roomId) {
            if (roomId === window.roomId)
                window.scheduleScopedDeviceRefresh()
        }

        function onServerRegistrationChanged(roomId, registered) {
            if (roomId !== window.roomId)
                return
            if (!registered) {
                window.registered = false
                window.roomId = ""
                window.backendReady = false
                api.stopEventStream()
                classroomFallbackDeviceTimer.stop()
                classroomEventRefreshTimer.stop()
                deviceModel.refresh()
                window.resetDeviceStats()
                window.statusText = "当前教室已取消接入"
            }
        }

        function onOperationFinished(action, message) {
            if (action === "command")
                window.statusText = "控制指令已发送，请稍候查看设备状态"
            else if (action === "report")
                window.statusText = "报修已提交，请等待管理员处理"
            else
                window.statusText = message
        }
        function onError(message) {
            window.pendingBulkAction = ""
            window.statusText = message
        }
    }
}
