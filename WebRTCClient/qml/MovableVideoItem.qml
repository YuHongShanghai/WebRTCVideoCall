import QtQuick
import QtQuick.Controls
import VideoItem 1.0


Rectangle {
    id: root
    property string videoItemName: ""
    property var rootWindow
    property var siblingVideoItem
    property bool fillParent: false
    anchors.fill: fillParent ? parent : undefined
    z: fillParent ? 0 : 999

    VideoItem {
        id: videoItem
        objectName: videoItemName
        anchors.fill: parent
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        enabled: !root.fillParent
        drag.target: root
        drag.minimumX: 0
        drag.maximumX: root.parent.width - videoItem.width
        drag.minimumY: 0
        drag.maximumY: root.parent.height - videoItem.height

        onClicked: {
            var cx = root.x
            var cy = root.y
            var cw = root.width
            var ch = root.height
            root.fillParent = !root.fillParent
            //dragArea.enabled = !root.fillParent
            if (!root.fillParent) {
                setGeometry(siblingVideoItem.x, siblingVideoItem.y, siblingVideoItem.width, siblingVideoItem.height)
            }
            siblingVideoItem.fillParent = !siblingVideoItem.fillParent
            if (!siblingVideoItem.fillParent) {
                siblingVideoItem.setGeometry(cx, cy, cw, ch)
            }
        }
    }

    function setGeometry(nx, ny, w, h) {
        root.x = nx
        root.y = ny
        root.width = w
        root.height = h
    }
}