import QtQuick 2.15
import "." as Common

/*
 * SurfaceCard 只提供统一卡片外观。
 * 如果它被放进 Layout，调用方仍要显式给出 implicit/preferred 尺寸，不能依赖 anchors 让 Layout 猜大小。
 */
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
