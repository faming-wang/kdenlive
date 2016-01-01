import QtQuick.Controls 1.3
import QtQuick.Controls.Styles 1.3
import QtQuick 2.0

Item {
    id: root
    objectName: "root"

    // default size, but scalable by user
    height: 300; width: 400
    property string markerText
    property string timecode
    property point profile
    property double scale
    property bool showMarkers
    property bool showTimecode
    property bool showSafezone
    property bool showAudiothumb
    signal editCurrentMarker()

    Item {
        id: frame
        objectName: "referenceframe"
        width: root.profile.x * root.scale
        height: root.profile.y * root.scale
        anchors.centerIn: parent
        visible: root.showSafezone
        Rectangle {
            id: safezone
            objectName: "safezone"
            color: "transparent"
            border.color: "cyan"
            width: parent.width * 0.9
            height: parent.height * 0.9
            anchors.centerIn: parent
            Rectangle {
              id: safetext
              objectName: "safetext"
              color: "transparent"
              border.color: "cyan"
              width: frame.width * 0.8
              height: frame.height * 0.8
              anchors.centerIn: parent
            }
        }
    }

    Text {
        id: timecode
        objectName: "timecode"
        color: "white"
        style: Text.Outline; 
        styleColor: "black"
        text: root.timecode
        visible: root.showTimecode
        font.pixelSize: root.height / 20
        anchors {
            right: root.right
            bottom: root.bottom
            rightMargin: 4
        }
    }

    TextField {
        id: marker
        objectName: "markertext"
        activeFocusOnPress: true
        onEditingFinished: {
            root.markerText = marker.displayText
            marker.focus = false
            root.editCurrentMarker()
        }

        anchors {
            left: parent.left
            bottom: parent.bottom
        }
        visible: root.showMarkers && text != ""
        maximumLength: 20
        text: root.markerText
        style: TextFieldStyle {
            textColor: "white"
            background: Rectangle {
                color: "#990000ff"
                width: marker.width
            }
        }
        font.pixelSize: frame.height / 25
    }
}
