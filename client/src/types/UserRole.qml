/*
 * File: UserRole.qml
 * Author: Justin Williams
 * Date: 3/24/26
 * File Description: A singleton that stores user role types. It also offers a toString function 
 * that accepts a role value and returns a string representing the value.
 */

pragma Singleton
import QtQuick

QtObject {
    readonly property int director: 0
    readonly property int camera: 1

    function toString(role, formatted=false) {
        switch (role) {
        case director:
            return formatted ? "Director" : "director";
        case camera:
            return formatted ? "Operator" : "camera";
        default:
            return "";
        }
    }
}
