import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactControls
import QGroundControl.Controls

Item {
    implicitWidth: mainLayout.implicitWidth
    implicitHeight: mainLayout.implicitHeight
    width: parent.width  // grows when Loader is wider than implicitWidth

    FactPanelController { id: controller; }

    property Fact   _returnAltFact:     controller.getParameterFact(-1, "RTL_RETURN_ALT")
    property Fact   _descendAltFact:    controller.getParameterFact(-1, "RTL_DESCEND_ALT")
    property Fact   _commRCLossFact:    controller.getParameterFact(-1, "COM_RC_LOSS_T")
    property Fact   _lowBattAction:     controller.getParameterFact(-1, "COM_LOW_BAT_ACT")
    property Fact   _rcLossAction:      controller.getParameterFact(-1, "NAV_RCL_ACT")
    property Fact   _dataLossAction:    controller.getParameterFact(-1, "NAV_DLL_ACT")
    property Fact   _rtlLandDelayFact:  controller.getParameterFact(-1, "RTL_LAND_DELAY")
    property int    _rtlLandDelayValue: _rtlLandDelayFact ? _rtlLandDelayFact.value : 0
    // COM_LOW_BAT_ACT=3 returns at critical battery and lands at emergency battery.
    // Use the parameter value because translated enum descriptions cannot be parsed as English.
    readonly property bool _stagedBatteryAction: _lowBattAction ? Number(_lowBattAction.rawValue) === 3 : false

    ColumnLayout {
        id: mainLayout
        width: parent.width
        spacing: 0

        VehicleSummaryRow {
            labelText: qsTr("Low Battery Failsafe")
            valueText: _stagedBatteryAction ? "" : (_lowBattAction ? _lowBattAction.enumStringValue : "")
        }

        VehicleSummaryRow {
            visible:   _stagedBatteryAction
            labelText: "  " + qsTr("Critical Level")
            valueText: qsTr("Return")
        }

        VehicleSummaryRow {
            visible:   _stagedBatteryAction
            labelText: "  " + qsTr("Emergency Level")
            valueText: qsTr("Land")
        }

        VehicleSummaryRow {
            labelText: qsTr("RC/Joystick Loss Failsafe")
            valueText: _rcLossAction ? _rcLossAction.enumStringValue : ""
        }

        VehicleSummaryRow {
            labelText: qsTr("RC/Joystick Loss Timeout")
            valueText: _commRCLossFact ? _commRCLossFact.valueString + " " + _commRCLossFact.units : ""
        }

        VehicleSummaryRow {
            labelText: qsTr("Data Link Loss Failsafe")
            valueText: _dataLossAction ? _dataLossAction.enumStringValue : ""
        }

        VehicleSummaryRow {
            labelText: qsTr("RTL Climb To")
            valueText: _returnAltFact ? _returnAltFact.valueString + " " + _returnAltFact.units : ""
        }

        VehicleSummaryRow {
            labelText: qsTr("RTL, Then")
            valueText: _rtlLandDelayValue === 0 ?
                           qsTr("Land immediately") :
                           (_rtlLandDelayValue < 0 ?
                                qsTr("Loiter and do not land") :
                                qsTr("Loiter and land after specified time"))

        }

        VehicleSummaryRow {
            labelText: qsTr("Loiter Alt")
            valueText: _descendAltFact ? _descendAltFact.valueString + " " + _descendAltFact.units : ""
            visible:    _descendAltFact && _rtlLandDelayValue !== 0
        }

        VehicleSummaryRow {
            labelText: qsTr("Land Delay")
            valueText: _rtlLandDelayFact ? _rtlLandDelayValue + " " + _rtlLandDelayFact.units : ""
            visible:    _rtlLandDelayValue > 0
        }
    }
}
