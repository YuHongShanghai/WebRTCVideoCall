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

        function onAsrText(text, end) {
            console.log(text, end)
            if (end) {
                asrPanel.finalAsrText = asrPanel.finalAsrText + text
                asrPanel.curAsrText = ""
            } else {
                asrPanel.curAsrText = text
            }
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

    Button {
        id: closeBtn
        width: 30
        height: 30
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: callBtn.bottom
        anchors.topMargin: 120
        hoverEnabled: true
        background: Rectangle {
            anchors.fill: parent
            color: "transparent"
            Image {
                anchors.fill: parent
                source: closeBtn.hovered ? "qrc:/resources/close_hovered.svg" : "qrc:/resources/close.svg"
            }
        }
        onClicked: Qt.quit()
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

            Rectangle {
                anchors.fill: parent
                visible: !controller.remoteVideoEnabled
                color: "#021122"
                Text {
                    text: root.remoteId
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 20
                }
            }
        }

        MovableVideoItem {
            id: localVideoItem
            rootWindow: root
            siblingVideoItem: remoteVideoItem
            fillParent: false
            videoItemName: "localVideo"

            Rectangle {
                anchors.fill: parent
                visible: !controller.videoEnabled
                color: "#021122"
                Text {
                    text: controller.localId
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 20
                }
            }
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
                        source: audionControlBtn.hovered ?
                            (controller.audioEnabled ? "qrc:/resources/audio_on_hovered.svg": "qrc:/resources/audio_off_hovered.svg") :
                            (controller.audioEnabled ? "qrc:/resources/audio_on.svg": "qrc:/resources/audio_off.svg")
                    }
                }
                onClicked: {
                    controller.audioEnabled = !controller.audioEnabled
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
                        source: videoControlBtn.hovered ?
                            (controller.videoEnabled ? "qrc:/resources/video_on_hovered.svg": "qrc:/resources/video_off_hovered.svg") :
                            (controller.videoEnabled ? "qrc:/resources/video_on.svg": "qrc:/resources/video_off.svg")
                    }
                }
                onClicked: {
                    controller.videoEnabled = !controller.videoEnabled
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

            Button {
                id: asrBtn
                width: 80
                height: 80
                hoverEnabled: true
                background: Rectangle {
                    anchors.fill: parent
                    color: asrBtn.hovered ? "white" : "transparent"
                    radius: 40

                    Image {
                        anchors.centerIn: parent
                        source: asrBtn.hovered ? "qrc:/resources/asr_hovered.svg" : "qrc:/resources/asr.svg"
                    }
                }
                onClicked: {
                    if (asrPanel.y === -asrPanel.height) {
                        controller.startAsr()
                        asrPanelSlideIn.start()
                    }
                    else {
                        asrPanelSlideOut.start()
                        controller.stopAsr()
                    }
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

    AsrPanel {
        id: asrPanel
        height: parent.height*0.35
        width: parent.width*0.5
        anchors.horizontalCenter: parent.horizontalCenter
        y: -asrPanel.height
        color: "#4D021122"

        NumberAnimation {
            id: asrPanelSlideIn
            target: asrPanel
            property: "y"
            from: -asrPanel.height
            to: 10
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            id: asrPanelSlideOut
            target: asrPanel
            property: "y"
            from: 10
            to: -asrPanel.height
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
