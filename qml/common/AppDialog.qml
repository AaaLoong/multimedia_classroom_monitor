import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "." as Common

/*
 * 复杂弹窗统一走这个骨架：
 * 1. 不在业务页里直接手拼 Dialog + Layout，避免重复踩尺寸约束坑。
 * 2. Layout 子项必须有明确尺寸来源，所以内容区固定为标题头 + 可滚动正文。
 */
Dialog {
    id: root

    property string dialogTitle: ""
    property int viewportMargin: 28
    property int preferredDialogWidth: 640
    property int preferredDialogHeight: 560
    property int minimumDialogWidth: 420
    property int minimumDialogHeight: 260
    property int bodyPadding: Common.Theme.spaceXL
    property int bodySpacing: Common.Theme.spaceL
    readonly property real bodyViewportHeight: bodyViewport.height
    readonly property real bodyViewportWidth: bodyViewport.width

    default property alias bodyData: bodyColumn.data

    modal: true
    focus: true
    padding: 0
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.max(minimumDialogWidth,
                    Math.min(preferredDialogWidth,
                             Math.max(minimumDialogWidth,
                                      (parent ? parent.width : preferredDialogWidth) - viewportMargin * 2)))
    height: Math.max(minimumDialogHeight,
                     Math.min(preferredDialogHeight,
                              Math.max(minimumDialogHeight,
                                       (parent ? parent.height : preferredDialogHeight) - viewportMargin * 2)))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    background: Rectangle {
        radius: Common.Theme.radiusL
        color: Common.Theme.cardBg
        border.width: 1
        border.color: Common.Theme.borderColor
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: titleText.implicitHeight + Common.Theme.spaceXL * 2
            color: "#f7faff"
            border.width: 0
            radius: Common.Theme.radiusL

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Common.Theme.radiusL
                color: parent.color
            }

            Text {
                id: titleText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Common.Theme.spaceXL
                text: root.dialogTitle
                color: Common.Theme.textPrimary
                font.family: Common.Theme.fontFamily
                font.pixelSize: 30
                font.bold: true
                wrapMode: Text.WordWrap
            }
        }

        Item {
            id: bodyViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Flickable {
                id: bodyFlick
                anchors.fill: parent
                contentWidth: width
                contentHeight: bodyColumn.implicitHeight + root.bodyPadding * 2
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                clip: true

                ColumnLayout {
                    id: bodyColumn
                    x: root.bodyPadding
                    y: root.bodyPadding
                    width: Math.max(0, bodyFlick.width - root.bodyPadding * 2)
                    spacing: root.bodySpacing
                }
            }
        }
    }
}
