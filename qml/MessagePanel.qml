import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    visible: true
    width: 400
    height: 600

    signal message(string msg)

    ListModel {
        id: chatModel
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5

        ListView {
            id: messageView
            Layout.fillWidth: true
            Layout.fillHeight: true // 确保 ListView 占据所有剩余空间
            spacing: 10
            model: chatModel
            clip: true

            onContentHeightChanged: {
                if (contentHeight > height) {
                    positionViewAtEnd()
                }
            }

            delegate:
                Text {
                    id: displayText
                    text: model.text
                    wrapMode: Text.WordWrap
                    width: parent.width * 0.7
                    anchors.right: isOutgoing ? parent.right : undefined
                    anchors.left: isOutgoing ? undefined : parent.left
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    color: isOutgoing ? "#C7E5F9" : "#E6AA63"
                    horizontalAlignment: isOutgoing ? Text.AlignRight : Text.AlignLeft
                }
        }

        RowLayout {
            height: 50
            spacing: 5

            Rectangle {
                Layout.fillWidth: true
                height: 40
                radius: 5
                color: "#1e1e1e"
                border.color: "#3a8dde"
                border.width: 2

                TextInput {
                    id: messageInput
                    anchors.fill: parent
                    anchors.margins: 8
                    color: "#ffffff"
                    font.pixelSize: 16
                    selectionColor: "#3399ff"
                    selectedTextColor: "white"

                    Keys.onReturnPressed: sendButton.clicked()

                    cursorVisible: messageInput.focus
                    cursorDelegate: Rectangle {
                        width: 1
                        color: "white"
                        radius: 1
                    }
                }
            }

            Button {
                id: sendButton
                text: "发送"
                height: 40
                enabled: messageInput.text.length > 0

                onClicked: {
                    var msg = messageInput.text
                    if (msg.trim().length > 0) {
                        message(msg)
                        chatModel.append({
                            "text": msg,
                            "isOutgoing": true
                        })

                        messageInput.text = ""
                        messageView.positionViewAtEnd()
                    }
                }
            }
        }
    }

    function setRemoteMessage(message) {
        chatModel.append({
            "text": message,
            "isOutgoing": false
        })
    }
}
