import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    property double lastShowTs: 0

    Component {
        id: riseImageComponent
        RiseImage { }
    }

    function show(x, y, type) {
        var ts = Date.now()
        if (ts - lastShowTs < 500) {
            return
        }

        var imgSource = ""
        if (type === "ok") {
            imgSource = "qrc:/resources/gesture_ok.svg"
        } else if (type === "one") {
            imgSource = "qrc:/resources/gesture_one.svg"
        } else if (type === "like") {
            imgSource = "qrc:/resources/gesture_like.svg"
        } else if (type === "dislike") {
            imgSource = "qrc:/resources/gesture_dislike.svg"
        } else if (type === "peace") {
            imgSource = "qrc:/resources/gesture_peace.svg"
        } else if (type === "palm") {
            imgSource = "qrc:/resources/gesture_palm.svg"
        } else if (type === "holy") {
            imgSource = "qrc:/resources/gesture_holy.svg"
        } else if (type === "hand_heart2" || type === "hand_heart") {
            imgSource = "qrc:/resources/gesture_hand_heart.svg"
        } else if (type === "middle_finger") {
            imgSource = "qrc:/resources/gesture_middle_finger.svg"
        } else if (type === "fist") {
            imgSource = "qrc:/resources/gesture_fist.svg"
        }

        if (imgSource.length > 0) {
            showImg(imgSource, x, y)
            lastShowTs = ts
        }
    }

    function showImg(source, x, y) {
        var obj = riseImageComponent.createObject(root)
        obj.imgX = x
        obj.imgY = y
        obj.imgSource = source
        obj.imgSize = root.height/5
        obj.play()
        obj.destroy(3000)
    }
}