// Component: FlyViewPreFlightChecklistPopup
// Purpose: Fly view checklist popup dialog. Extends QGCPopupDialog with a "Pre-Flight Checklist"
//   title and a Close button. Provides a simple overlay for reviewing checklist from flight view.
// Properties:
//   (inherits all QGCPopupDialog properties)
import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.Vehicle
import QGroundControl.Controls

QGCPopupDialog {
    id:         _root
    title:      qsTr("Pre-Flight Checklist")
    buttons:    StandardButton.Close
}
