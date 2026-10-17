import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

GroupBox {
    id: editor

    property var members: []

    signal chooseScenario()

    function addScenario(file) {
        if (members.length >= 8)
            return false;

        var next = members.slice(0);
        var number = 1;
        while (next.some(function(row) {
            return row.id === "case-" + number;
        }))++number
        next.push({
            "id": "case-" + number,
            "scenario": file.toString()
        });
        members = next;
        return true;
    }

    function moveMember(from, to) {
        if (from < 0 || to < 0 || from >= members.length || to >= members.length)
            return false;

        var next = members.slice(0);
        var member = next.splice(from, 1)[0];
        next.splice(to, 0, member);
        members = next;
        return true;
    }

    function removeMember(index) {
        if (index < 0 || index >= members.length)
            return false;

        var next = members.slice(0);
        next.splice(index, 1);
        members = next;
        return true;
    }

    objectName: "researchBatchEditor"
    title: "Состав серии · " + members.length + " / 8"

    Column {
        width: parent.width
        spacing: 10

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Добавьте готовые сценарии в порядке выполнения. Один файл можно повторить. Каждый сценарий должен задавать reset_replay=true и подходить к одной подключённой записи. Его эталон разрешается от исходного каталога."
        }

        Repeater {
            model: editor.members

            delegate: Column {
                width: parent.width
                spacing: 5

                RowLayout {
                    width: parent.width

                    Label {
                        Layout.fillWidth: true
                        text: (index + 1) + ". " + modelData.id
                        font.bold: true
                    }

                    Button {
                        objectName: "batchMoveUp_" + modelData.id
                        text: "↑"
                        Accessible.name: "Переместить сценарий выше"
                        enabled: index > 0
                        onClicked: editor.moveMember(index, index - 1)
                    }

                    Button {
                        objectName: "batchMoveDown_" + modelData.id
                        text: "↓"
                        Accessible.name: "Переместить сценарий ниже"
                        enabled: index + 1 < editor.members.length
                        onClicked: editor.moveMember(index, index + 1)
                    }

                    Button {
                        objectName: "batchRemove_" + modelData.id
                        text: "Удалить"
                        onClicked: editor.removeMember(index)
                    }

                }

                Label {
                    width: parent.width
                    wrapMode: Text.WrapAnywhere
                    text: modelData.scenario
                }

            }

        }

        Button {
            objectName: "batchAddScenario"
            text: "Добавить сценарий…"
            enabled: editor.members.length < 8
            onClicked: editor.chooseScenario()
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Состав фиксируется при запуске. После проверки входов план сохраняется в inputs/plan.json нового каталога отчётов. Это ссылки на исходные файлы; данные не копируются."
        }

    }

}
