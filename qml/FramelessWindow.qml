import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Window {
    id: root
    width: 640
    height: 480
    visible: true

    flags: Qt.Window | Qt.FramelessWindowHint

    color: "transparent"

    Rectangle {
        anchors.fill: parent; color: "transparent"
        radius: root.visibility === 4 ? 0 : 15

        Item {
            anchors.fill: parent

            DragHandler {
                onActiveChanged: {
                    if (active) {
                        root.startSystemMove()
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                onDoubleClicked: root.visibility = root.visibility == 4 ? 2 : 4
            }
        }
        Item {
            anchors.fill: parent
            enabled: root.visibility !== 4  //窗口最大化时禁用

            Item    //上边缘调整窗口大小
            {
                width: parent.width; height: 5

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeVerCursor   //改变鼠标样式，让用户知道现在正处于调整窗口大小状态，带ver的是上下箭头，带hor的是左右箭头
                    onPressed: root.startSystemResize(Qt.TopEdge)
                }
            }

            Item    //下边缘调整窗口大小
            {
                width: parent.width; height: 5; anchors.bottom: parent.bottom

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeVerCursor
                    onPressed: root.startSystemResize(Qt.BottomEdge)
                }
            }

            Item    //左边缘调整窗口大小
            {
                width: 5; height: parent.height

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    onPressed: root.startSystemResize(Qt.LeftEdge)
                }
            }

            Item    //右边缘调整窗口大小
            {
                width: 5; height: parent.height; anchors.right: parent.right

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    onPressed: root.startSystemResize(Qt.RightEdge)
                }
            }

            Item    //左上角调整窗口大小
            {
                width: 5; height: 5

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeFDiagCursor
                    onPressed: root.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
                }
            }

            Item    //右上角调整窗口大小
            {
                width: 5; height: 5; anchors.right: parent.right

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeBDiagCursor
                    onPressed: root.startSystemResize(Qt.RightEdge | Qt.TopEdge)
                }
            }

            Item    //左下角调整窗口大小
            {
                width: 5; height: 5; anchors.bottom: parent.bottom

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeBDiagCursor
                    onPressed: root.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
                }
            }

            Item    //右下角调整窗口大小
            {
                width: 5; height: 5; anchors.bottom: parent.bottom; anchors.right: parent.right

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeFDiagCursor
                    onPressed: root.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
                }
            }
        }
    }
}
