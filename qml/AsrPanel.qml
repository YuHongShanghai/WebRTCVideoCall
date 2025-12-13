import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    width: 400
    height: 600
    color: "black"

    property string finalAsrText: ""
    property string curAsrText: ""

    Flickable {
        id: flickable
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight

        property int autoScrollThreshold: 20

        function isNearBottom() {
            return contentY + height >= contentHeight - autoScrollThreshold
        }

        Column {
            id: column
            width: flickable.width
            spacing: 0
            anchors.top: parent.top

            // final text
            Rectangle {
                width: parent.width
                color: "transparent"
                implicitHeight: finalText.implicitHeight

                Text {
                    id: finalText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 4
                    wrapMode: Text.Wrap
                    text: root.finalAsrText
                    font.pixelSize: 16
                    color: "#E6AA63"
                }
            }

            // current text
            Rectangle {
                width: parent.width
                color: root.curAsrText.length > 0 ? "#55EEEEEE" : "transparent"
                radius: 3
                implicitHeight: curText.implicitHeight + 8

                Text {
                    id: curText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 4
                    wrapMode: Text.Wrap
                    text: root.curAsrText
                    font.pixelSize: 16
                    color: "#E6AA63"
                }
            }
        }

        onContentHeightChanged: {
            if (isNearBottom()) {
                contentY = Math.max(0, contentHeight - height)
            }
        }
    }
}