import QtQuick 2.15
import QtQuick.Controls 2.15
import "." as Common

Button {
    id: root

    property string variant: "primary"
    property int minWidth: 110
    property int maxWidth: 220
    readonly property color baseBackground: {
        if (root.variant === "secondary")
            return Common.Theme.cardBg
        if (root.variant === "ghost")
            return Common.Theme.accentSoft
        if (root.variant === "danger")
            return Common.Theme.danger
        return Common.Theme.accent
    }
    readonly property color hoverBackground: {
        if (root.variant === "secondary")
            return "#f4f8ff"
        if (root.variant === "ghost")
            return "#dce8ff"
        if (root.variant === "danger")
            return "#a83d55"
        return "#255bc2"
    }
    readonly property color pressedBackground: {
        if (root.variant === "secondary")
            return "#e9eff8"
        if (root.variant === "ghost")
            return "#cfddfb"
        if (root.variant === "danger")
            return "#8f3247"
        return "#1f4ea9"
    }
    readonly property color textColor: {
        if (!root.enabled)
            return "#8a97aa"
        if (root.variant === "secondary")
            return Common.Theme.textPrimary
        if (root.variant === "ghost")
            return Common.Theme.accent
        return "white"
    }
    readonly property color frameColor: {
        if (!root.enabled)
            return "#d3dbe6"
        if (root.variant === "secondary")
            return root.down ? "#bdd0f2" : (root.hovered ? "#9fbaf0" : Common.Theme.borderColor)
        if (root.variant === "ghost")
            return root.down ? "#a9c2ef" : (root.hovered ? "#8daeee" : "#d7e5ff")
        return Qt.rgba(255, 255, 255, root.hovered ? 0.24 : 0.0)
    }

    hoverEnabled: root.enabled
    implicitWidth: Math.min(Math.max(minWidth, textMetrics.implicitWidth + root.leftPadding + root.rightPadding), maxWidth)
    implicitHeight: 42
    leftPadding: 18
    rightPadding: 18
    topPadding: 10
    bottomPadding: 10
    font.family: Common.Theme.fontFamily
    font.pixelSize: 14
    transformOrigin: Item.Center
    scale: root.enabled && root.down ? 0.98 : 1.0
    clip: true

    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }

    background: Rectangle {
        radius: Common.Theme.radiusM
        border.width: root.variant === "secondary" || root.variant === "ghost" ? 1 : (root.hovered ? 1 : 0)
        border.color: root.frameColor
        color: !root.enabled
            ? "#dfe5ee"
            : (root.down ? root.pressedBackground : (root.hovered ? root.hoverBackground : root.baseBackground))

        Behavior on color {
            ColorAnimation { duration: 120 }
        }

        Behavior on border.width {
            NumberAnimation { duration: 90 }
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: root.variant === "secondary" || root.variant === "ghost"
                ? Common.Theme.accent
                : "white"
            opacity: !root.enabled ? 0.0 : (root.down ? 0.12 : (root.hovered ? 0.06 : 0.0))

            Behavior on opacity {
                NumberAnimation { duration: 120 }
            }
        }
    }

    contentItem: Text {
        id: textMetrics
        text: root.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
        wrapMode: Text.NoWrap
        color: root.textColor
        font.family: Common.Theme.fontFamily
        font.pixelSize: 14
        font.bold: true
    }

    HoverHandler {
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
