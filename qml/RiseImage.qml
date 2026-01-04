import QtQuick
import QtQuick.Controls

Item {
    id: root
    property alias imgSource: img.source
    property alias imgX: img.x
    property alias imgY: riseAnimation.from
    property int imgSize: 100

    Image {
        id: img
        width: root.imgSize
        height: root.imgSize
        opacity: 0.5
        visible: y > -root.imgSize
    }

    PropertyAnimation {
        id: riseAnimation
        target: img
        property: "y"
        to: -100
        duration: 3000
        easing.type: Easing.Linear
    }

    function play() {
        riseAnimation.start()
    }

}

