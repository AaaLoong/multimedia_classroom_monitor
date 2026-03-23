import QtQuick 2.15
import "." as Common

Rectangle {
    id: root

    property color fillColor: Common.Theme.cardBg
    property color outlineColor: Common.Theme.borderColor
    property int padding: Common.Theme.spaceXL
    property bool clipContent: true

    default property alias contentData: contentItem.data

    color: fillColor
    radius: Common.Theme.radiusL
    border.width: 1
    border.color: outlineColor
    clip: root.clipContent

    Item {
        id: contentItem
        anchors.fill: parent
        anchors.margins: root.padding
        clip: root.clipContent
    }
}
