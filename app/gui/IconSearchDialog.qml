import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.3

NavigableDialog {
    id: dialog

    property int appIndex: -1
    property string appName: ""
    property var appModel: null
    property var searchResults: []
    property string selectedIconUrl: ""
    property string selectedThumbUrl: ""
    property bool searching: false
    property bool downloading: false
    property string statusText: ""

    signal browseLocalFileRequested(int appIndex, string appName)

    modal: true
    width: Math.min(680, parent ? parent.width - 40 : 680)
    height: Math.min(560, parent ? parent.height - 40 : 560)
    anchors.centerIn: Overlay.overlay

    function showForApp(index, name, model) {
        appIndex = index
        appName = name
        appModel = model
        selectedIconUrl = ""
        selectedThumbUrl = ""
        searchResults = []
        searching = false
        downloading = false
        statusText = ""
        if (engineDropdown) {
            engineDropdown.currentIndex = 0 // Google is default
        }
        searchInput.text = name + " game icon png"
        open()
        triggerSearch()
    }

    function triggerSearch() {
        var query = searchInput.text.trim()
        if (query.length === 0 || !appModel) return
        searching = true
        downloading = false
        var engine = (engineDropdown && engineDropdown.currentText) ? engineDropdown.currentText : "Google"
        statusText = qsTr("Searching %1 for icons...").arg(engine)
        searchResults = []
        selectedIconUrl = ""
        selectedThumbUrl = ""
        appModel.searchIcons(query, engine)
    }

    function onSearchFinished(results, error) {
        searching = false
        if (error.length > 0) {
            statusText = qsTr("Search error: %1").arg(error)
        } else {
            searchResults = results
            if (results.length === 0) {
                statusText = qsTr("No icons found online. Try modifying the search term or choose a local file.")
            } else {
                statusText = qsTr("Found %1 icons. Click an icon to select, or choose a file from your computer.").arg(results.length)
            }
        }
    }

    function confirmCreate() {
        if (selectedIconUrl.length === 0 || downloading || !appModel) return
        downloading = true
        statusText = qsTr("Downloading selected icon...")
        appModel.downloadAndCreateShortcut(appIndex, selectedIconUrl, selectedThumbUrl)
    }

    onOpened: {
        searchInput.forceActiveFocus()
        searchInput.selectAll()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // Header Title & Subtitle
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: qsTr("Select Icon for %1").arg(appName)
                font.pointSize: 17
                font.bold: true
                color: Material.primaryTextColor
                elide: Label.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Search for an icon online, or add one yourself from your computer.")
                font.pointSize: 11
                color: Material.secondaryTextColor
                Layout.fillWidth: true
            }
        }

        // Search Input Bar with Engine Dropdown
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                id: engineDropdown
                model: ["Google", "DuckDuckGo"]
                currentIndex: 0 // Google is default
                Layout.preferredWidth: 140
                enabled: !downloading

                onActivated: {
                    triggerSearch()
                }
            }

            TextField {
                id: searchInput
                Layout.fillWidth: true
                placeholderText: qsTr("Search for game icons (e.g. %1 icon)...").arg(appName)
                selectByMouse: true
                enabled: !downloading

                Keys.onReturnPressed: triggerSearch()
                Keys.onEnterPressed: triggerSearch()
            }

            Button {
                text: qsTr("Search")
                enabled: !searching && !downloading && searchInput.text.trim().length > 0
                onClicked: triggerSearch()
            }
        }

        // Status Label / Spinner
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: statusText.length > 0 || searching || downloading

            BusyIndicator {
                running: searching || downloading
                visible: running
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }

            Label {
                text: statusText
                font.pointSize: 11
                color: (searching || downloading) ? Material.primaryTextColor : Material.secondaryTextColor
                elide: Label.ElideRight
                Layout.fillWidth: true
            }
        }

        // Grid Area of Icon Results
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#161616"
            radius: 6
            border.color: "#30FFFFFF"
            border.width: 1
            clip: true

            ScrollView {
                id: scrollArea
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                GridView {
                    id: iconGrid
                    width: scrollArea.width
                    cellWidth: 106
                    cellHeight: 106
                    model: searchResults
                    focus: true

                    delegate: Item {
                        width: iconGrid.cellWidth
                        height: iconGrid.cellHeight

                        property bool isSelected: selectedIconUrl === modelData.fullUrl

                        Rectangle {
                            anchors.centerIn: parent
                            width: 94
                            height: 94
                            radius: 8
                            color: isSelected ? "#384556" : (itemMouseArea.containsMouse ? "#2A2A2A" : "#1F1F1F")
                            border.color: isSelected ? Material.accent : (itemMouseArea.containsMouse ? "#80FFFFFF" : "#30FFFFFF")
                            border.width: isSelected ? 3 : 1

                            Image {
                                anchors.fill: parent
                                anchors.margins: 8
                                fillMode: Image.PreserveAspectFit
                                source: modelData.thumbUrl
                                asynchronous: true
                                smooth: true
                            }

                            MouseArea {
                                id: itemMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor

                                onClicked: {
                                    selectedIconUrl = modelData.fullUrl
                                    selectedThumbUrl = modelData.thumbUrl
                                }

                                onDoubleClicked: {
                                    selectedIconUrl = modelData.fullUrl
                                    selectedThumbUrl = modelData.thumbUrl
                                    confirmCreate()
                                }
                            }

                            ToolTip.visible: itemMouseArea.containsMouse && modelData.title && modelData.title.length > 0
                            ToolTip.text: modelData.title ? modelData.title : ""
                            ToolTip.delay: 400
                        }
                    }
                }
            }

            // Empty state placeholder
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: !searching && !downloading && searchResults.length === 0

                Label {
                    text: qsTr("No icons available")
                    font.pointSize: 14
                    font.bold: true
                    color: Material.secondaryTextColor
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: qsTr("Use the search bar above or choose a local file from disk.")
                    font.pointSize: 11
                    color: Material.secondaryTextColor
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // Bottom Footer Controls
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                id: browseLocalButton
                text: qsTr("Browse Local File...")
                enabled: !downloading
                onClicked: {
                    browseLocalFileRequested(appIndex, appName)
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: createButton
                text: qsTr("Create Shortcut")
                highlighted: true
                enabled: selectedIconUrl.length > 0 && !downloading && !searching
                onClicked: confirmCreate()
            }

            Button {
                id: cancelButton
                text: qsTr("Cancel")
                enabled: !downloading
                onClicked: dialog.close()
            }
        }
    }
}

