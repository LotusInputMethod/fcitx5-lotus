# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
D-Bus handler to communicate with Fcitx5 Controller.
"""

import dbus


class LotusDBusHandler:
    def __init__(self):
        self.addon_name = "fcitx://config/addon/lotus"
        try:
            self.bus = dbus.SessionBus()
            self.proxy = self.bus.get_object("org.fcitx.Fcitx5", "/controller")
            self.iface = dbus.Interface(self.proxy, "org.fcitx.Fcitx.Controller1")
        except dbus.DBusException as e:
            print(f"Fcitx5 D-Bus Error: {e}")
            self.iface = None

    def get_config(self) -> dict:
        """Get config from Fcitx5 and convert to Python dict/list."""
        if not self.iface:
            return {}
        try:
            values, metadata = self.iface.GetConfig(self.addon_name)
            return {
                "values": self._clean_dbus(values),
                "metadata": self._clean_dbus(metadata),
            }
        except Exception as e:
            print(f"Failed to fetch config: {e}")
            return {}

    def set_config(self, values_dict: dict) -> bool:
        """Set config and send to Fcitx5. Returns True on success, False otherwise."""
        if not self.iface:
            return False
        try:
            dbus_dict = self._prepare_dbus_data(values_dict)
            self.iface.SetConfig(self.addon_name, dbus_dict)
            return True
        except Exception as e:
            print(f"Failed to set config: {e}")
            return False

    def get_sub_config_list(self, path: str, root_key: str) -> list:
        """Get sub config list from Fcitx5 and convert to Python list."""
        if not self.iface:
            return []
        try:
            full_path = f"{self.addon_name}/{path}"
            values, metadata = self.iface.GetConfig(full_path)
            clean_values = self._clean_dbus(values)

            array_dict = clean_values.get(root_key, {})
            if isinstance(array_dict, dict):
                sorted_keys = sorted(
                    array_dict.keys(),
                    key=lambda k: (0, int(k)) if str(k).isdigit() else (1, str(k)),
                )
                return [array_dict[k] for k in sorted_keys]
            elif isinstance(array_dict, list):
                return array_dict
            return []
        except Exception as e:
            print(f"Failed to fetch sub config ({path}): {e}")
            return []

    def set_sub_config_list(self, path: str, root_key: str, data_list: list) -> bool:
        """Set sub config list and send to Fcitx5. Returns True on success, False otherwise."""
        if not self.iface:
            return False
        try:
            full_path = f"{self.addon_name}/{path}"
            fcitx_array = {str(i): item for i, item in enumerate(data_list)}
            dbus_payload = {root_key: fcitx_array}
            dbus_dict = self._prepare_dbus_data(dbus_payload)
            self.iface.SetConfig(full_path, dbus_dict)
            return True
        except Exception as e:
            print(f"Failed to set sub config ({path}): {e}")
            return False

    def _prepare_dbus_data(self, data):
        """Prepare data to be sent to Fcitx5 in dbus types with signatures."""
        if isinstance(data, dict):
            # Fcitx5 expects dicts to be a{sv} (String to Variant)
            formatted = {str(k): self._prepare_dbus_data(v) for k, v in data.items()}
            return dbus.Dictionary(formatted, signature="sv")
        elif isinstance(data, list):
            # Arrays must be Array of Variants (av)
            formatted = [self._prepare_dbus_data(v) for v in data]
            return dbus.Array(formatted, signature="v")
        elif isinstance(data, bool):
            return dbus.Boolean(data)
        elif isinstance(data, int):
            return dbus.Int32(data)
        elif isinstance(data, float):
            return dbus.Double(data)
        elif data is None:
            return dbus.String("")
        else:
            return dbus.String(str(data))

    def _clean_dbus(self, data):
        """Convert dbus types to Python types."""
        if isinstance(data, dbus.Dictionary):
            return {str(k): self._clean_dbus(v) for k, v in data.items()}
        elif isinstance(data, (dbus.Array, dbus.Struct, list, tuple)):
            return [self._clean_dbus(v) for v in data]
        elif isinstance(data, dbus.Boolean):
            return bool(data)
        elif isinstance(
            data,
            (dbus.Int16, dbus.Int32, dbus.Int64, dbus.UInt16, dbus.UInt32, dbus.UInt64),
        ):
            return int(data)
        elif isinstance(data, dbus.Double):
            return float(data)
        elif isinstance(data, dbus.String):
            return str(data)
        return data
