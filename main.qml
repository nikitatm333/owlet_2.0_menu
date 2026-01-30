import QtQuick 2.12
import QtQuick.Window 2.12

Window {
    visible: true
    width: 1280
    height: 720
    title: "Camera Preview"

    Rectangle {
        anchors.fill: parent
        color: "#000000"

        Image {
            id: camImage
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            // source uses image provider "camera" defined in main.cpp
            // token is cameraObj.token and updates each frame (forces reload)
            source: "image://camera/live?token=" + (cameraObj ? cameraObj.token : "0")
            asynchronous: false
            cache: false
        }

        Text {
            text: "Device: /dev/video0"
            color: "white"
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 8
            font.pixelSize: 16
        }
    }
}
