import QtQuick 2.15
import "." as Common

Rectangle {
    id: root

    property string text: ""
    property color toneColor: Common.Theme.accent
    property color fillColor: Common.Theme.accentSoft
    property int maxWidth: 180

    implicitWidth: Math.min(label.implicitWidth + 22, root.maxWidth)
    implicitHeight: 32
    radius: implicitHeight / 2
    color: fillColor
    border.width: 1
    border.color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.16)
    clip: true

    Text {
        id: label
        anchors.centerIn: parent
        width: parent.width - 16
        text: root.text
        color: root.toneColor
        font.family: Common.Theme.fontFamily
        font.pixelSize: 12
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        maximumLineCount: 1
        wrapMode: Text.NoWrap
    }
}
