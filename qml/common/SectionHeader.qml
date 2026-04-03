import QtQuick 2.15
import QtQuick.Layouts 1.15
import "." as Common

Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property int maxTitleLines: 1
    property int maxSubtitleLines: 2

    default property alias actionData: actionsRow.data

    implicitHeight: Math.max(textColumn.implicitHeight, actionsRow.implicitHeight)
    clip: false

    RowLayout {
        anchors.fill: parent
        spacing: Common.Theme.spaceM

        ColumnLayout {
            id: textColumn
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: root.title
                Layout.fillWidth: true
                color: Common.Theme.textPrimary
                font.family: Common.Theme.fontFamily
                font.pixelSize: 24
                font.bold: true
                elide: Text.ElideRight
                maximumLineCount: root.maxTitleLines
                wrapMode: root.maxTitleLines > 1 ? Text.WordWrap : Text.NoWrap
            }

            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                Layout.fillWidth: true
                color: Common.Theme.textSecondary
                font.family: Common.Theme.fontFamily
                font.pixelSize: 13
                elide: Text.ElideRight
                maximumLineCount: root.maxSubtitleLines
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            id: actionsRow
            spacing: Common.Theme.spaceS
            Layout.alignment: Qt.AlignRight | Qt.AlignTop
        }
    }
}
