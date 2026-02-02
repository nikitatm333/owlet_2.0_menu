import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12

Window {
    id: root
    visible: true
    width: 1280
    height: 720
    title: "Camera Preview"

    signal menuAction(string action, string value)

    Rectangle {
        anchors.fill: parent
        color: "#000000"

        Image {
            id: camImage
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            source: "image://camera/live?token=" + (cameraObj ? cameraObj.token : "0")
            asynchronous: false
            cache: false
        }

        Text {
            text: "Press the M button to open the menu"
            color: "white"
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 8
            font.pixelSize: 16
        }

        // --- Menu Overlay ---
        Item {
            id: menuRoot
            anchors.centerIn: parent
            width: parent.width * 0.5
            height: parent.height * 0.6
            visible: false
            focus: true

            Rectangle {
                id: overlay
                anchors.fill: parent
                color: "#000000"
                opacity: 0.45
                radius: 14
                border.color: "#ffffff22"
                border.width: 1
                z: 1
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 12
                z: 2

                Text {
                    id: header
                    text: menuRoot.menuStack.length === 0 ? "Owlet2.0 menu" : menuRoot.menuStack[menuRoot.menuStack.length - 1].display
                    font.pixelSize: 28
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    Layout.alignment: Qt.AlignHCenter
                }

                Rectangle { height: 1; color: "#ffffff22"; Layout.fillWidth: true }

                ListView {
                    id: list
                    focus: false
                    model: menuRoot.menuModel
                    currentIndex: menuRoot.currentIndex
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    highlightFollowsCurrentItem: true

                    delegate: Item {
                        width: parent.width
                        height: 56

                        property bool isCurrent: ListView.isCurrentItem

                        Rectangle {
                            anchors.fill: parent
                            color: isCurrent ? "#ffffff" : "transparent"
                            opacity: isCurrent ? 0.95 : 0
                            radius: 8
                            z: -1
                        }

                        Text {
                            text: modelData.display
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 18
                            color: isCurrent ? "#111111" : "white"
                            font.pixelSize: 20
                        }
                    }

                    Keys.onPressed: {
                        // handle navigation when list has focus
                        if (event.key === Qt.Key_Up) {
                            if (menuRoot.currentIndex > 0) menuRoot.currentIndex--;
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Down) {
                            if (menuRoot.currentIndex < menuRoot.menuModel.length - 1) menuRoot.currentIndex++;
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            menuRoot.handleEnter();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Escape) {
                            menuRoot.handleEsc();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Q) {
                            menuRoot.closeAll();
                            event.accepted = true;
                        }
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Text { text: "Enter - Выбрать"; color: "#dddddd"; font.pixelSize: 14 }
                    Text { text: "|"; color: "#dddddd"; font.pixelSize: 14 }
                    Text { text: "Esc - Назад"; color: "#dddddd"; font.pixelSize: 14 }
                    Text { text: "|"; color: "#dddddd"; font.pixelSize: 14 }
                    Text { text: "Q - Закрыть"; color: "#dddddd"; font.pixelSize: 14 }
                }
            }

            Keys.onPressed: {
                // allow navigation even if list didn't get focus
                if (event.key === Qt.Key_Up) {
                    if (menuRoot.currentIndex > 0) menuRoot.currentIndex--;
                    event.accepted = true;
                } else if (event.key === Qt.Key_Down) {
                    if (menuRoot.currentIndex < menuRoot.menuModel.length - 1) menuRoot.currentIndex++;
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    menuRoot.handleEnter();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Escape) {
                    menuRoot.handleEsc();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Q) {
                    menuRoot.closeAll();
                    event.accepted = true;
                } else if (event.key === Qt.Key_M) {
                    menuRoot.toggle();
                    event.accepted = true;
                }
            }

            property int currentIndex: 0
            property var menuStack: []
            property var menuModel: []

            function buildModel(name) {
                var arr = [];
                if (name === "main") {
                    arr = [ {display: "Контраст", id: "contrast"}, {display: "Палитра", id: "palette"}, {display: "Траектория", id: "trajectory"}, {display: "Кадр", id: "frame"}];
                } else if (name === "contrast") {
                    arr = [ {display: "Позитив", id: "positive"}, {display: "Негатив", id: "negative"}];
                } else if (name === "palette") {
                    arr = [ {display: "Белый горячий", id: "white_hot"}, {display: "Черный горячий", id: "black_hot"}, {display: "Сепия", id: "sepia"}, {display: "Красный горячий", id: "red_hot"}, {display: "Железо", id: "iron"}, {display: "Зеленый холодный", id: "green_cold"}];
                } else if (name === "trajectory") {
                    arr = [ {display: "Да", id: "traj_yes"}, {display: "Нет", id: "traj_no"}];
                } else if (name === "frame") {
                    arr = [ {display: "Гамма-кор", id: "gamma"}, {display: "Контраст", id: "frame_contrast"}, {display: "Шарпенинг", id: "sharpen"}, {display: "Яркость кадра", id: "brightness"}];
                }
                return arr;
            }

            function openMenu() {
                menuRoot.menuStack = [];
                menuRoot.menuModel = menuRoot.buildModel("main");
                menuRoot.currentIndex = 0;
                menuRoot.visible = true;
                // ensure list gets keyboard focus
                focusTimer.start();
            }

            function toggle() {
                if (menuRoot.visible) {
                    menuRoot.closeAll();
                } else {
                    menuRoot.openMenu();
                }
            }

            function handleEnter() {
                var item = menuRoot.menuModel[menuRoot.currentIndex];
                if (!item) return;

                if (menuRoot.menuStack.length === 0) {
                    menuRoot.menuStack.push({id: item.id, display: item.display});
                    menuRoot.menuModel = menuRoot.buildModel(item.id);
                    menuRoot.currentIndex = 0;
                    focusTimer.start();
                    return;
                }

                var parent = menuRoot.menuStack[menuRoot.menuStack.length - 1];
                menuRoot.menuAction(parent.id, item.display);
                console.log("menuAction:", parent.id, item.display);
                menuRoot.closeAll();
            }

            function handleEsc() {
                if (menuRoot.menuStack.length > 0) {
                    menuRoot.menuStack.pop();
                    if (menuRoot.menuStack.length === 0) {
                        menuRoot.menuModel = menuRoot.buildModel("main");
                    } else {
                        var parent = menuRoot.menuStack[menuRoot.menuStack.length - 1];
                        menuRoot.menuModel = menuRoot.buildModel(parent.id);
                    }
                    menuRoot.currentIndex = 0;
                    focusTimer.start();
                } else {
                    menuRoot.closeAll();
                }
            }

            function closeAll() {
                visible = false;
                menuRoot.menuStack = [];
                menuRoot.menuModel = [];
            }

            // small timer to set focus on list after visible toggled
            Timer {
                id: focusTimer
                interval: 0
                repeat: false
                onTriggered: {
                    // try to give focus to ListView; if fails, give focus to menuRoot
                    if (!list.focus) {
                        list.focus = true;
                        list.forceActiveFocus();
                    } else {
                        menuRoot.forceActiveFocus();
                    }
                }
            }

            Component.onCompleted: {
                menuRoot.menuModel = menuRoot.buildModel("main");
            }
        }

        Keys.onPressed: {
            if (event.key === Qt.Key_M) {
                menuRoot.toggle();
                event.accepted = true;
            }
        }
    }
}
