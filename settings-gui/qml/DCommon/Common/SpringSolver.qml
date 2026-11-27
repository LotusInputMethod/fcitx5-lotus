pragma Singleton

import QtQuick
import "SpringMath.js" as SpringMath

QtObject {
    function coefficients(stiffness, damping, mass, seconds) {
        return SpringMath.coefficients(stiffness, damping, mass, seconds);
    }

    function frameCoefficients(stiffness, damping, mass, frameTime) {
        return coefficients(stiffness, damping, mass, frameTime);
    }
}
