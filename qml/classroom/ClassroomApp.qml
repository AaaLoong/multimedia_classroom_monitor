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
    property string selectedBuilding: ""
    property string selectedFloor: ""
    property string selectedRoomSuffix: ""
    property string manualRoomId: ""
    property string inputRoomId: manualRoomId.trim() !== ""
        ? manualRoomId.trim()
        : ((selectedBuilding !== "" && selectedFloor !== "" && selectedRoomSuffix !== "")
            ? (selectedBuilding + selectedFloor + selectedRoomSuffix)
            : "")
    property bool locked: api.locked
    property bool manualRefresh: false
    property bool bindingChecking: true
    property bool backendReady: false
    property bool registered: api.registered
    property string statusText: "正在验证终端身份..."
    property string pendingBulkAction: ""
    property string awaitingBulkCompletionAction: ""
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
            window.statusText = "请先输入或选择教室号"
            return ""
        }
        if (!window.manualRoomId.trim() && !window.validateRoomId(rid)) {
            window.statusText = "教室号不在允许范围内"
            return ""
        }
        return rid
    }

    function beginBindingLookup() {
        window.bindingChecking = true
        window.statusText = "正在验证终端身份..."
        api.fetchBindingByFingerprint(terminalFingerprint)
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
            window.awaitingBulkCompletionAction = ""
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

        window.awaitingBulkCompletionAction = action
        window.statusText = (action === "power_on" ? "一键上课已下发 " : "一键下课已下发 ")
            + sendCount + " 台设备"
            + (skippedOffline > 0 ? ("，跳过 " + skippedOffline + " 台离线设备") : "")
    }

    function hasBulkActionCompleted(action, devices) {
        if (!action || !devices || devices.length === 0)
            return false

        let matched = 0
        for (let i = 0; i < devices.length; ++i) {
            const d = devices[i]
            const st = String(d.status ? d.status : "").toLowerCase()
            if (st === "offline")
                continue
            matched += 1
            if (action === "power_on" && st !== "on")
                return false
            if (action === "power_off" && st !== "off")
                return false
        }
        return matched > 0
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
        if (typeof initialRoomId !== "undefined" && initialRoomId)
            applyRoomIdSelection(initialRoomId)
        deviceModel.refresh()
        api.fetchLock()
        window.beginBindingLookup()
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
        width: 520
        height: 320
        x: (window.width - width) / 2
        y: (window.height - height) / 2
        standardButtons: Dialog.NoButton

        background: Common.SurfaceCard {
            padding: 0
        }

        contentItem: Item {
            anchors.fill: parent
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: Common.Theme.spaceXL
                    spacing: Common.Theme.spaceL

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Common.Theme.spaceM

                        Rectangle {
                            width: 54
                            height: 54
                            radius: 18
                            color: "#e9f1ff"

                            Text {
                                anchors.centerIn: parent
                                text: "修"
                                color: Common.Theme.accent
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 24
                                font.bold: true
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: "设备报修"
                                color: Common.Theme.textPrimary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 28
                                font.bold: true
                            }

                            Text {
                                width: parent.width
                                text: "设备：" + window.displayDeviceTitle(window.pendingReportDevice.name, window.pendingReportDevice.deviceId)
                                color: Common.Theme.textSecondary
                                font.family: Common.Theme.fontFamily
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Common.Theme.radiusM
                        color: "#f7faff"
                        border.width: 1
                        border.color: reportInput.activeFocus ? Common.Theme.accent : "#cfd8e6"

                        TextArea {
                            id: reportInput
                            anchors.fill: parent
                            anchors.margins: 2
                            color: Common.Theme.textPrimary
                            font.family: Common.Theme.fontFamily
                            font.pixelSize: 16
                            inputMethodHints: Qt.ImhMultiLine
                            wrapMode: TextEdit.Wrap
                            leftPadding: 16
                            rightPadding: 16
                            topPadding: 16
                            bottomPadding: 16
                            selectByMouse: true
                            background: Item {}
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: reportInput.leftPadding + 2
                            anchors.rightMargin: reportInput.rightPadding + 2
                            anchors.topMargin: reportInput.topPadding + 2
                            visible: reportInput.length === 0 && !reportInput.preeditText
                            text: "请描述设备问题、故障现象和出现时间..."
                            color: "#9aa9bf"
                            font.family: reportInput.font.family
                            font.pixelSize: reportInput.font.pixelSize
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#e4ebf5"
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 86

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Common.Theme.spaceL
                        spacing: Common.Theme.spaceM

                        Item { Layout.fillWidth: true }

                        Common.ActionButton {
                            text: "取消"
                            variant: "secondary"
                            minWidth: 104
                            onClicked: reportDialog.close()
                        }

                        Common.ActionButton {
                            text: "提交"
                            minWidth: 128
                            enabled: reportInput.text.trim().length > 0
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
        }
    }

    Component {
        id: classroomDeviceDelegate

        Rectangle {
            required property string deviceId
            required property string name
            required property string status
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
        bindingChecking: window.bindingChecking
        backendReady: window.backendReady
        roomId: window.roomId
        headerTitle: window.registered ? ("教室 " + window.roomId + " 控制台") : "教室接入控制台"
        inputRoomId: window.inputRoomId
        manualRoomText: window.manualRoomId
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
        target: form.manualRoomField
        function onTextChanged() { window.manualRoomId = form.manualRoomField.text }
    }

    Connections {
        target: form.registerButton
        function onClicked() {
            const rid = window.currentRoomIdOrError()
            if (!rid)
                return
            api.registerClassroomWithFingerprint(rid, terminalFingerprint)
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

            deviceModel.setDevices(scoped)
            window.updateDeviceStats(scoped)

            if (window.awaitingBulkCompletionAction
                    && window.hasBulkActionCompleted(window.awaitingBulkCompletionAction, scoped)) {
                window.statusText = window.awaitingBulkCompletionAction === "power_on"
                    ? "已执行上课"
                    : "已执行下课"
                window.awaitingBulkCompletionAction = ""
            }

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
                const resolvedRoomId = api.boundRoomId ? api.boundRoomId.trim() : ""
                if (!resolvedRoomId) {
                    window.registered = false
                    window.bindingChecking = false
                    window.backendReady = false
                    window.roomId = ""
                    api.stopEventStream()
                    classroomFallbackDeviceTimer.stop()
                    classroomEventRefreshTimer.stop()
                    deviceModel.refresh()
                    window.resetDeviceStats()
                    window.awaitingBulkCompletionAction = ""
                    window.statusText = "未获取到有效房号，请重新接入"
                    return
                }
                window.roomId = resolvedRoomId
                window.bindingChecking = false
                if (!window.manualRoomId.trim() && window.validateRoomId(resolvedRoomId))
                    window.applyRoomIdSelection(resolvedRoomId)
                window.statusText = "终端已绑定教室，正在同步设备..."
                window.manualRefresh = false
                api.startClassroomEventStream(window.roomId)
                window.rescheduleFallbackPolling()
                api.fetchDevicesByRoom(window.roomId)
            } else {
                window.bindingChecking = false
                window.statusText = "教室未注册，请先完成接入"
                window.backendReady = false
                window.roomId = ""
                window.awaitingBulkCompletionAction = ""
                api.stopEventStream()
                classroomFallbackDeviceTimer.stop()
                classroomEventRefreshTimer.stop()
                deviceModel.refresh()
                window.resetDeviceStats()
            }
        }

        function onRegistrationCheckedChanged(checked) {
            if (checked && !window.registered) {
                window.bindingChecking = false
                window.statusText = "当前终端尚未绑定教室，请完成接入注册"
                window.backendReady = false
            } else if (checked && window.registered) {
                window.bindingChecking = false
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
                window.bindingChecking = false
                window.roomId = ""
                window.backendReady = false
                window.awaitingBulkCompletionAction = ""
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
            else if (action === "register")
                window.statusText = "终端绑定成功，正在同步设备..."
            else
                window.statusText = message
        }
        function onError(message) {
            window.pendingBulkAction = ""
            window.awaitingBulkCompletionAction = ""
            if (window.bindingChecking)
                window.bindingChecking = false
            window.statusText = message
        }
    }
}
