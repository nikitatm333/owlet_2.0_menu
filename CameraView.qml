import QtQuick 2.0
import QtQuick.Controls 2.0

Item {
    id: root

    property alias source: cameraView.source
    property alias running: cameraView.running
    property alias cameraIndex: cameraView.cameraIndex

    Rectangle {
        anchors.fill: parent
        color: "black"

        Image {
            id: cameraView
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            cache: false

            property var source: cameraController
            property bool running: false
            property int cameraIndex: 0

            onRunningChanged: {
                if (running) {
                    cameraController.startCamera(cameraIndex);
                } else {
                    cameraController.stopCamera();
                }
            }

            onCameraIndexChanged: {
                cameraController.selectCamera(cameraIndex);
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: cameraView.status === Image.Loading
            visible: running
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: "Camera " + (cameraView.cameraIndex + 1)
        color: "white"
        font.pixelSize: 20
        style: Text.Outline
        styleColor: "black"
    }
}
