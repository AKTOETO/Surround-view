import QtQuick 2.6

Item {
    id: guard

    property var targetWindow: null
    property var controller: researchController
    property bool closeWhenDone: false

    visible: false

    Connections {
        function onClosing(event) {
            if (guard.controller.busy) {
                event.accepted = false;
                guard.closeWhenDone = true;
                guard.controller.cancel();
            }
        }

        target: guard.targetWindow
    }

    Connections {
        function onChanged() {
            if (guard.closeWhenDone && !guard.controller.busy && guard.targetWindow) {
                guard.closeWhenDone = false;
                guard.targetWindow.close();
            }
        }

        target: guard.controller
    }

}
