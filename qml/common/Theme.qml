pragma Singleton

import QtQuick 2.15

QtObject {
    readonly property string fontFamily: Qt.fontFamilies().indexOf("PingFang SC") !== -1 ? "PingFang SC"
        : (Qt.fontFamilies().indexOf("Hiragino Sans GB") !== -1 ? "Hiragino Sans GB"
        : (Qt.fontFamilies().indexOf("Microsoft YaHei") !== -1 ? "Microsoft YaHei" : "sans-serif"))

    readonly property color pageBg: "#f4f7fb"
    readonly property color panelBg: "#fbfdff"
    readonly property color cardBg: "#ffffff"
    readonly property color borderColor: "#dce4ee"
    readonly property color textPrimary: "#162033"
    readonly property color textSecondary: "#627084"
    readonly property color accent: "#2f67d8"
    readonly property color accentSoft: "#eaf0ff"
    readonly property color success: "#1f8f6a"
    readonly property color successSoft: "#e8f6f1"
    readonly property color warning: "#b7791f"
    readonly property color warningSoft: "#fff6e8"
    readonly property color danger: "#bf4b63"
    readonly property color dangerSoft: "#fdecef"
    readonly property color neutralSoft: "#eef2f7"
    readonly property color inkDark: "#16263d"
    readonly property color inkDarkSoft: "#d8e5f8"

    readonly property int spaceS: 8
    readonly property int spaceM: 12
    readonly property int spaceL: 16
    readonly property int spaceXL: 20
    readonly property int radiusS: 12
    readonly property int radiusM: 18
    readonly property int radiusL: 26

    function severityLabel(level) {
        if (level === "critical")
            return "严重";
        if (level === "repair")
            return "报修";
        return "警告";
    }

    function severityColor(level) {
        if (level === "critical")
            return danger;
        if (level === "repair")
            return warning;
        return "#c36b2b";
    }

    function severityFill(level) {
        if (level === "critical")
            return dangerSoft;
        if (level === "repair")
            return warningSoft;
        return "#fff1e6";
    }

    function deviceStatusLabel(status) {
        if (status === "on")
            return "运行中";
        if (status === "starting")
            return "启动中";
        if (status === "fault")
            return "故障";
        if (status === "offline")
            return "离线";
        return "待机";
    }

    function deviceStatusColor(status) {
        if (status === "on")
            return success;
        if (status === "starting")
            return accent;
        if (status === "fault" || status === "offline")
            return danger;
        return textSecondary;
    }

    function deviceStatusFill(status) {
        if (status === "on")
            return successSoft;
        if (status === "starting")
            return accentSoft;
        if (status === "fault" || status === "offline")
            return dangerSoft;
        return neutralSoft;
    }
}
