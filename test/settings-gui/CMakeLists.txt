# Headless Python unit tests for the settings-gui. _stubs.py replaces
# gi/dbus, so the suite needs only the Python standard library.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(LOTUS_PY_SETTINGS_GUI_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Registers one CTest entry per Python test module, mirroring the isolation,
# labels and timeout convention of add_lotus_unit_test().
#
# Usage:
#   add_lotus_python_test(<ctest_name> <test_file.py> "<labels>")
# Example:
#   add_lotus_python_test(settings_gui_i18n test_i18n.py "unit;settings-gui")
function(add_lotus_python_test name test_file labels)
    add_test(NAME ${name}
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${LOTUS_PY_SETTINGS_GUI_TEST_DIR}"
            -t "${LOTUS_PY_SETTINGS_GUI_TEST_DIR}"
            -p "${test_file}"
            -v
    )
    set_tests_properties(${name} PROPERTIES LABELS "${labels}" TIMEOUT 60)
endfunction()

add_lotus_python_test(settings_gui_i18n test_i18n.py "unit;settings-gui;python")
add_lotus_python_test(settings_gui_dbus_handler test_dbus_handler.py "unit;settings-gui;python")
add_lotus_python_test(settings_gui_adw_ui test_adw_ui.py "unit;settings-gui;python")
add_lotus_python_test(settings_gui_smoke_imports test_smoke_imports.py "unit;settings-gui;python;smoke")
