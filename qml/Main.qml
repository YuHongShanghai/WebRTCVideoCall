import QtQuick
import QtQuick.Controls
import WebrtcClient 1.0

FramelessWindow {
    id: root
    width: 800
    height: 600
    visible: true
    title: controller.localId
    color: "#021122"

    property string remoteId: "";

    Connections {
        target: controller
        function onRemoteJoined(id) {
            console.log("onRemoteJoined", id);
            root.remoteId = id
            remoteJoinedState()
        }

        function onRemoteLeft(id) {
            console.log("onRemoteLeft", id);
            root.remoteId = ""
            remoteLeftState()
        }

        function onRemoteIds(ids) {
            console.log("onRemoteIds", ids);
            if (ids.length > 0) {
                root.remoteId = ids[0]
                remoteJoinedState()
            }
        }

        function onPcStateChanged(state) {
            console.log("onPcStateChanged", state);
            if (state === Controller.Connecting) {
                callingState()
            } else if (state === Controller.Connected) {
                inCallState()
            } else if (state === Controller.Closed) {
                remoteJoinedState()
            }
        }

        function onPcClosed(id) {
            remoteJoinedState()
        }

        function onRemoteVideoSizeChanged(width, height) {
            if (!remoteVideoItem.fillParent) {
                return
            }
            root.width = width
            root.height = height
        }

        function onLocalVideoSizeChanged(width, height) {
            if (localVideoItem.fillParent) {
                return
            }

            localVideoItem.width = width / 5
            localVideoItem.height = height / 5
            var margin = 10
            localVideoItem.y = margin
            localVideoItem.x = margin
        }

        function onRemoteMessage(msg) {
            console.log("remote message", msg)
            if (messagePanel.x === root.width)
                messagePanelSlideIn.start()
            messagePanel.setRemoteMessage(msg)
        }
    }

    Connections {
        target: messagePanel
        function onMessage(msg) {
            console.log("send message", msg);
            controller.sendMessage(msg)
        }
    }

    Component.onCompleted: {
        controller.initVideoItem(root)
        remoteLeftState()
    }

    Rectangle {
        id: callBtn
        width: 120
        height: 120
        anchors.centerIn: parent
        radius: width / 2
        color: "transparent"
        border.color: "green"
        border.width: 2
        property string showText: "";

        Text {
            text: callBtn.showText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 20
        }

        MouseArea {
            id: callBtnMouseArea
            anchors.fill: parent
            hoverEnabled: true

            Image {
                id: imgBg
                anchors.centerIn: parent
                source: "qrc:/resources/ic_call.svg";
                visible: false
            }

            onClicked: {
                imgBg.visible = true;
                controller.callRemote(root.remoteId)
            }
            onEntered: {
                callBtn.showText = "";
                imgBg.visible = true;
            }
            onExited: {
                callBtn.showText = root.remoteId;
                imgBg.visible = false;
            }
        }
    }

    Rectangle {
        id: videoArea
        anchors.fill: parent

        MovableVideoItem {
            id: remoteVideoItem
            rootWindow: root
            siblingVideoItem: localVideoItem
            fillParent: true
            videoItemName: "remoteVideo"
        }

        MovableVideoItem {
            id: localVideoItem
            rootWindow: root
            siblingVideoItem: remoteVideoItem
            fillParent: false
            videoItemName: "localVideo"
        }

        Button {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 20
            anchors.bottomMargin: 20
            width: 80
            height: 80
            background: Image {
                anchors.centerIn: parent
                source: "qrc:/resources/ic_more.svg"
            }
            onClicked: {
                controlBtns.panelVisible = !controlBtns.panelVisible
            }
        }

        Row {
            id: controlBtns
            spacing: 10
            property bool panelVisible: true
            anchors.horizontalCenter: parent.horizontalCenter

            y: panelVisible ? parent.height - height - 50 : parent.height

            Behavior on y {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.OutQuad
                }
            }

            Button {
                id: audionControlBtn
                width: 80
                height: 80
                hoverEnabled: true
                background: Rectangle {
                    anchors.fill: parent
                    color: audionControlBtn.hovered ? "white" : "transparent"
                    radius: 40

                    Image {
                        anchors.centerIn: parent
                        source: audionControlBtn.hovered ? "qrc:/resources/audio_on_hovered.svg" : "qrc:/resources/audio_on.svg"
                    }
                }
            }

            Button {
                id: hungupBtn
                width: 80
                height: 80
                hoverEnabled: true
                background: Rectangle {
                    anchors.fill: parent
                    color: hungupBtn.hovered ? "white" : "transparent"
                    radius: 40

                    Image {
                        anchors.centerIn: parent
                        source: "qrc:/resources/ic_hungup.svg"
                    }
                }

                onClicked: {
                    controller.hungup()
                    remoteJoinedState()
                }
            }

            Button {
                id: videoControlBtn
                width: 80
                height: 80
                hoverEnabled: true
                background: Rectangle {
                    anchors.fill: parent
                    color: videoControlBtn.hovered ? "white" : "transparent"
                    radius: 40

                    Image {
                        anchors.centerIn: parent
                        source: videoControlBtn.hovered ? "qrc:/resources/video_on_hovered.svg" : "qrc:/resources/video_on.svg"
                    }
                }
            }

            Button {
                id: messageBtn
                width: 80
                height: 80
                hoverEnabled: true
                background: Rectangle {
                    anchors.fill: parent
                    color: messageBtn.hovered ? "white" : "transparent"
                    radius: 40

                    Image {
                        anchors.centerIn: parent
                        source: messageBtn.hovered ? "qrc:/resources/message_hovered.svg" : "qrc:/resources/message.svg"
                    }
                }
                onClicked: {
                    if (messagePanel.x === root.width)
                        messagePanelSlideIn.start()
                    else
                        messagePanelSlideOut.start()
                }
            }
        }
    }

    MessagePanel {
        id: messagePanel
        height: parent.height
        width: parent.width*0.35
        x: root.width
        y: 0
        color: "#4D021122"

        NumberAnimation {
            id: messagePanelSlideIn
            target: messagePanel
            property: "x"
            from: root.width
            to: root.width - messagePanel.width
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            id: messagePanelSlideOut
            target: messagePanel
            property: "x"
            from: root.width - messagePanel.width
            to: root.width
            duration: 200
            easing.type: Easing.InCubic
        }
    }


    function remoteLeftState() {
        callBtn.visible = false
        videoArea.visible = false
        root.width = 800
        root.height = 600
    }

    function remoteJoinedState() {
        callBtn.visible = true
        callBtnMouseArea.enabled = true
        callBtn.enabled = true
        callBtn.border.color = "green"
        callBtn.showText = root.remoteId
        videoArea.visible = false
        root.width = 800
        root.height = 600
    }

    function callingState() {
        callBtn.visible = true
        callBtnMouseArea.enabled = false
        callBtn.enabled = false
        callBtn.border.color = "red"
        callBtn.showText = root.remoteId
        videoArea.visible = false
        root.width = 800
        root.height = 600
    }

    function inCallState() {
        callBtn.visible = false
        videoArea.visible = true
    }
}
