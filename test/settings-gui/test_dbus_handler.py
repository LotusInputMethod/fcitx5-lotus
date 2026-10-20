# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for core.dbus_handler.LotusDBusHandler."""

import unittest
from unittest import mock

import _stubs
from core.dbus_handler import LotusDBusHandler

ADDON_NAME = "fcitx://config/addon/lotus"


def _handler(iface):
    """Builds a handler around an explicit controller interface fake."""
    handler = LotusDBusHandler.__new__(LotusDBusHandler)
    handler.addon_name = ADDON_NAME
    handler.iface = iface
    return handler


class FakeController:
    """Records GetConfig/SetConfig traffic like the Fcitx5 controller."""

    def __init__(self, values=None, metadata=None, error=None, set_error=None):
        self.values = values if values is not None else {}
        self.metadata = metadata if metadata is not None else {}
        self.error = error
        self.set_error = set_error
        self.get_calls = []
        self.set_calls = []

    def GetConfig(self, path):
        self.get_calls.append(path)
        if self.error is not None:
            raise self.error
        return self.values, self.metadata

    def SetConfig(self, path, payload):
        self.set_calls.append((path, payload))
        if self.set_error is not None:
            raise self.set_error


class LotusDBusHandlerInitTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()
        self._session_bus = self.dbus.SessionBus
        self._interface = self.dbus.Interface

    def tearDown(self):
        self.dbus.SessionBus = self._session_bus
        self.dbus.Interface = self._interface

    def test_binds_session_controller(self):
        bus = mock.MagicMock(name="bus")
        proxy = mock.MagicMock(name="proxy")
        iface = mock.MagicMock(name="iface")
        bus.get_object.return_value = proxy
        self.dbus.SessionBus = mock.MagicMock(return_value=bus)
        self.dbus.Interface = mock.MagicMock(return_value=iface)

        handler = LotusDBusHandler()

        self.assertIs(handler.iface, iface)
        self.assertEqual(handler.addon_name, ADDON_NAME)
        bus.get_object.assert_called_once_with("org.fcitx.Fcitx5", "/controller")
        self.dbus.Interface.assert_called_once_with(proxy, "org.fcitx.Fcitx.Controller1")

    def test_missing_session_bus_degrades_gracefully(self):
        self.dbus.SessionBus = mock.MagicMock(side_effect=self.dbus.DBusException("no session bus"))

        handler = LotusDBusHandler()

        self.assertIsNone(handler.iface)
        self.assertEqual(handler.get_config(), {})
        self.assertFalse(handler.set_config({"A": "1"}))
        self.assertFalse(handler.save_values({"A": "1"}))
        self.assertEqual(handler.get_sub_config_list("modes", "ModeList"), [])
        self.assertFalse(handler.set_sub_config_list("modes", "ModeList", [{"X": "1"}]))


class PrepareDbusDataTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()
        self.handler = _handler(mock.MagicMock(name="iface"))

    def test_scalar_type_mapping(self):
        cases = [
            (True, self.dbus.Boolean),
            (False, self.dbus.Boolean),
            (7, self.dbus.Int32),
            (2.5, self.dbus.Double),
            ("abc", self.dbus.String),
            (None, self.dbus.String),
        ]
        for value, expected_type in cases:
            with self.subTest(value=value):
                prepared = self.handler._prepare_dbus_data(value)
                self.assertIs(type(prepared), expected_type)

    def test_none_becomes_empty_string(self):
        prepared = self.handler._prepare_dbus_data(None)
        self.assertEqual(str(prepared), "")

    def test_bool_is_not_mapped_to_int(self):
        # isinstance(True, int) is True in Python: the bool branch must win.
        prepared = self.handler._prepare_dbus_data(True)
        self.assertIsNot(type(prepared), self.dbus.Int32)
        self.assertIs(type(prepared), self.dbus.Boolean)

    def test_dict_is_wrapped_as_sv_dictionary(self):
        prepared = self.handler._prepare_dbus_data({"A": 1})
        self.assertIs(type(prepared), self.dbus.Dictionary)
        self.assertEqual(prepared.signature, "sv")
        self.assertIs(type(prepared["A"]), self.dbus.Int32)

    def test_list_is_wrapped_as_v_array(self):
        prepared = self.handler._prepare_dbus_data([1, "x"])
        self.assertIs(type(prepared), self.dbus.Array)
        self.assertEqual(prepared.signature, "v")

    def test_nested_structures_keep_types(self):
        payload = {"Outer": [{"Inner": 3}, "text"]}
        prepared = self.handler._prepare_dbus_data(payload)
        self.assertIs(type(prepared), self.dbus.Dictionary)
        self.assertIs(type(prepared["Outer"]), self.dbus.Array)
        self.assertIs(type(prepared["Outer"][0]), self.dbus.Dictionary)
        self.assertIs(type(prepared["Outer"][0]["Inner"]), self.dbus.Int32)
        self.assertIs(type(prepared["Outer"][1]), self.dbus.String)

    def test_dict_keys_are_stringified(self):
        prepared = self.handler._prepare_dbus_data({3: "x"})
        self.assertEqual(list(prepared.keys()), ["3"])

    def test_tuple_falls_back_to_string(self):
        prepared = self.handler._prepare_dbus_data((1, 2))
        self.assertIs(type(prepared), self.dbus.String)
        self.assertEqual(str(prepared), "(1, 2)")


class CleanDbusRoundTripTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()
        self.handler = _handler(mock.MagicMock(name="iface"))

    def test_round_trip_preserves_payload(self):
        payload = {"B": [1, True, "x", 2.5], "A": {"C": 3}, "D": "s"}
        cleaned = self.handler._clean_dbus(self.handler._prepare_dbus_data(payload))
        self.assertEqual(cleaned, payload)
        self.assertIs(cleaned["B"][1], True)
        self.assertIsInstance(cleaned["B"][0], int)
        self.assertIsInstance(cleaned["B"][3], float)

    def test_dbus_containers_become_python_types(self):
        values = self.dbus.Dictionary(
            {
                "List": self.dbus.Array([self.dbus.String("x")], signature="s"),
                "Pair": self.dbus.Struct((1, 2)),
            },
            signature="sv",
        )
        cleaned = self.handler._clean_dbus(values)
        self.assertEqual(cleaned, {"List": ["x"], "Pair": [1, 2]})
        self.assertIs(type(cleaned), dict)
        self.assertIs(type(cleaned["List"]), list)

    def test_scalars_are_unwrapped(self):
        cases = [
            (self.dbus.Boolean(True), True),
            (self.dbus.Int32(5), 5),
            (self.dbus.UInt64(6), 6),
            (self.dbus.Double(1.5), 1.5),
            (self.dbus.String("s"), "s"),
        ]
        for value, expected in cases:
            with self.subTest(value=type(value).__name__):
                cleaned = self.handler._clean_dbus(value)
                self.assertEqual(cleaned, expected)

    def test_plain_types_pass_through_unchanged(self):
        plain = {"A": [1, 2]}
        self.assertIs(self.handler._clean_dbus(plain), plain)


class GetConfigTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()

    def test_returns_values_and_metadata(self):
        iface = FakeController(values={"EnableDictionary": "True"}, metadata={"K": "V"})
        handler = _handler(iface)

        config = handler.get_config()

        self.assertEqual(config, {"values": {"EnableDictionary": "True"}, "metadata": {"K": "V"}})
        self.assertEqual(iface.get_calls, [ADDON_NAME])

    def test_converts_dbus_types(self):
        values = self.dbus.Dictionary(
            {"Words": self.dbus.Array([self.dbus.String("abc")], signature="s")},
            signature="sv",
        )
        iface = FakeController(values=values)
        handler = _handler(iface)

        config = handler.get_config()

        self.assertEqual(config["values"], {"Words": ["abc"]})

    def test_without_interface_returns_empty(self):
        handler = _handler(None)
        self.assertEqual(handler.get_config(), {})

    def test_error_returns_empty(self):
        handler = _handler(FakeController(error=RuntimeError("boom")))
        self.assertEqual(handler.get_config(), {})


class SetConfigTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()

    def test_sends_prepared_payload(self):
        iface = FakeController()
        handler = _handler(iface)

        self.assertTrue(handler.set_config({"EnableDictionary": "True"}))

        self.assertEqual(len(iface.set_calls), 1)
        path, payload = iface.set_calls[0]
        self.assertEqual(path, ADDON_NAME)
        self.assertEqual(payload.signature, "sv")
        self.assertEqual(handler._clean_dbus(payload), {"EnableDictionary": "True"})

    def test_error_returns_false(self):
        handler = _handler(FakeController(set_error=RuntimeError("boom")))
        self.assertFalse(handler.set_config({"A": "1"}))

    def test_without_interface_returns_false(self):
        handler = _handler(None)
        self.assertFalse(handler.set_config({"A": "1"}))


class SaveValuesTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()

    def test_merges_updates_into_current_config(self):
        iface = FakeController(values={"Keep": "yes", "A": "1"})
        handler = _handler(iface)

        self.assertTrue(handler.save_values({"A": "2"}))

        path, payload = iface.set_calls[0]
        self.assertEqual(path, ADDON_NAME)
        self.assertEqual(
            handler._clean_dbus(payload),
            {"Keep": "yes", "A": "2"},
        )

    def test_failed_fetch_keeps_interface_but_sends_nothing(self):
        iface = FakeController(error=RuntimeError("boom"))
        handler = _handler(iface)

        # fetch failure -> report error, send nothing
        self.assertFalse(handler.save_values({"A": "1"}))
        self.assertEqual(iface.set_calls, [])

    def test_without_interface_returns_false(self):
        handler = _handler(None)
        self.assertFalse(handler.save_values({"A": "1"}))


class GetSubConfigListTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()

    def test_sorts_numeric_keys_before_parsing(self):
        values = {
            "ModeList": {
                "10": "ten",
                "2": "two",
                "1": "one",
                "abc": "word",
            }
        }
        iface = FakeController(values=values)
        handler = _handler(iface)

        result = handler.get_sub_config_list("modes", "ModeList")

        self.assertEqual(result, ["one", "two", "ten", "word"])
        self.assertEqual(iface.get_calls, [f"{ADDON_NAME}/modes"])

    def test_list_root_is_returned_as_is(self):
        iface = FakeController(values={"ModeList": ["a", "b"]})
        handler = _handler(iface)
        self.assertEqual(handler.get_sub_config_list("modes", "ModeList"), ["a", "b"])

    def test_missing_root_key_returns_empty(self):
        iface = FakeController(values={"Other": {"0": "x"}})
        handler = _handler(iface)
        self.assertEqual(handler.get_sub_config_list("modes", "ModeList"), [])

    def test_scalar_root_returns_empty(self):
        iface = FakeController(values={"ModeList": "nope"})
        handler = _handler(iface)
        self.assertEqual(handler.get_sub_config_list("modes", "ModeList"), [])

    def test_error_returns_empty(self):
        handler = _handler(FakeController(error=RuntimeError("boom")))
        self.assertEqual(handler.get_sub_config_list("modes", "ModeList"), [])

    def test_without_interface_returns_empty(self):
        handler = _handler(None)
        self.assertEqual(handler.get_sub_config_list("modes", "ModeList"), [])


class SetSubConfigListTests(unittest.TestCase):
    def setUp(self):
        self.dbus = _stubs.dbus_stub()

    def test_sends_indexed_array_payload(self):
        iface = FakeController()
        handler = _handler(iface)
        data = [{"K": "1"}, {"K": "2"}]

        self.assertTrue(handler.set_sub_config_list("modes", "ModeList", data))

        path, payload = iface.set_calls[0]
        self.assertEqual(path, f"{ADDON_NAME}/modes")
        self.assertEqual(payload.signature, "sv")
        self.assertEqual(
            handler._clean_dbus(payload),
            {"ModeList": {"0": {"K": "1"}, "1": {"K": "2"}}},
        )

    def test_error_returns_false(self):
        handler = _handler(FakeController(set_error=RuntimeError("boom")))
        self.assertFalse(handler.set_sub_config_list("modes", "ModeList", [{"K": "1"}]))

    def test_without_interface_returns_false(self):
        handler = _handler(None)
        self.assertFalse(handler.set_sub_config_list("modes", "ModeList", [{"K": "1"}]))


if __name__ == "__main__":
    unittest.main()
