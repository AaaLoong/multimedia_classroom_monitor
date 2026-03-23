import QtQuick 2.15
import "." as Common

Common.SurfaceCard {
    id: root

    property string title: ""
    property string value: ""
    property string subtitle: ""
    property string iconText: ""
    property color toneColor: Common.Theme.accent
    property color toneFill: Common.Theme.accentSoft
    property int maxTitleLines: 1
    property int maxSubtitleLines: 2

    implicitWidth: 220
    implicitHeight: root.subtitle.length > 0 ? 188 : 164

    Column {
        anchors.fill: parent
        spacing: 10
        clip: true

        Rectangle {
            width: 42
            height: 42
            radius: Common.Theme.radiusS
            color: root.toneFill

            Text {
                anchors.centerIn: parent
                text: root.iconText
                color: root.toneColor
                font.family: Common.Theme.fontFamily
                font.pixelSize: 16
                font.bold: true
            }
        }

        Text {
            text: root.title
            width: parent.width
            color: Common.Theme.textPrimary
            font.family: Common.Theme.fontFamily
            font.pixelSize: 15
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: root.maxTitleLines
            wrapMode: root.maxTitleLines > 1 ? Text.WordWrap : Text.NoWrap
        }

        Text {
            text: root.value
            width: parent.width
            color: Common.Theme.textPrimary
            font.family: Common.Theme.fontFamily
            font.pixelSize: Math.max(24, Math.min(36, Math.round(root.width * 0.18)))
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 1
            wrapMode: Text.NoWrap
        }

        Text {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Common.Theme.textSecondary
            width: parent.width
            elide: Text.ElideRight
            maximumLineCount: root.maxSubtitleLines
            wrapMode: Text.WordWrap
            font.family: Common.Theme.fontFamily
            font.pixelSize: 12
        }
    }
}
