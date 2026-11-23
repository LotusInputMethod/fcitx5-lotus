# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Headless stand-ins for the qtpy, gi, and dbus dependencies of settings-gui.

Importing this module installs them into ``sys.modules``, so the test suite
only needs the Python standard library and no Qt binding, GTK4/Libadwaita
binding, D-Bus daemon or display server.
"""

import html
import sys
import types
import unittest.mock
import zlib
from pathlib import Path

# settings-gui lives at the repository root
SETTINGS_GUI_DIR = Path(__file__).resolve().parents[2] / "settings-gui"

# Real Qt::Modifier values, kept bit-distinct so flag arithmetic
# (``a | b``, ``a & b``) stays meaningful
_QT_MODIFIER_FLAGS = {
    ("Qt", "ShiftModifier"): 0x02000000,
    ("Qt", "ControlModifier"): 0x04000000,
    ("Qt", "AltModifier"): 0x08000000,
    ("Qt", "MetaModifier"): 0x10000000,
}


class _FakeMeta(type):
    """Serves arbitrary class-level attributes (e.g. ``QIcon.fromTheme``)."""

    def __getattr__(cls, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return unittest.mock.MagicMock()


class _LayoutItem:
    """Minimal ``QLayoutItem`` replacement exposing ``widget()``."""

    def __init__(self, widget):
        self._widget = widget

    def widget(self):
        return self._widget


class _FakeObject(metaclass=_FakeMeta):
    """Inert base class for the fake Qt classes.

    Provides a minimal layout container protocol (``count`` / ``itemAt`` /
    ``takeAt`` / ``addWidget``) and resolves other attributes to cached
    ``MagicMock`` objects.
    """

    def __init__(self, *args, **kwargs):
        self._items = []

    def addWidget(self, widget):
        self._items.append(widget)

    def count(self):
        return len(self._items)

    def itemAt(self, index):
        return _LayoutItem(self._items[index])

    def takeAt(self, index):
        return _LayoutItem(self._items.pop(index))

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        mock = unittest.mock.MagicMock()
        setattr(self, name, mock)
        return mock


class _QtNamespace:
    """Attribute bag whose members are stable, unique integers."""

    def __init__(self, namespace):
        self._namespace = namespace

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        known = _QT_MODIFIER_FLAGS.get((self._namespace, name))
        if known is not None:
            value = known
        else:
            value = zlib.crc32(f"{self._namespace}.{name}".encode()) & 0x0FFFFFFF
        setattr(self, name, value)
        return value


class _Signal:
    """Stand-in for ``qtpy.QtCore.Signal``; instance access returns a ``MagicMock``."""

    def __init__(self, *types_):
        self.types_ = types_

    def __set_name__(self, owner, name):
        self._attr = f"_signal_{name}"

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        mock = obj.__dict__.get(self._attr)
        if mock is None:
            mock = unittest.mock.MagicMock()
            setattr(obj, self._attr, mock)
        return mock


def _fake_class_module(name):
    """Builds a module that fabricates subclassable fake classes on demand."""
    module = types.ModuleType(name)

    def _fabricate(attr):
        if attr.startswith("__"):
            raise AttributeError(attr)
        fake = _FakeMeta(attr, (_FakeObject,), {})
        setattr(module, attr, fake)
        return fake

    module.__getattr__ = _fabricate
    return module


def _scalar(base, name):
    """Builds a typed dbus scalar stand-in (e.g. ``dbus.Int32``)."""

    def __new__(cls, value=0):
        return base.__new__(cls, base(value))

    return type(name, (base,), {"__new__": __new__})


def build_dbus_module():
    """Builds a fresh ``dbus`` stand-in module."""
    module = types.ModuleType("dbus")

    class DBusException(Exception):
        """Stand-in for ``dbus.DBusException``."""

    class Interface:
        """Stand-in for ``dbus.Interface``."""

        def __init__(self, proxy, interface_name):
            self.proxy = proxy
            self.interface_name = interface_name

    class Dictionary(dict):
        def __init__(self, data=None, signature=None):
            super().__init__(data or {})
            self.signature = signature

    class Array(list):
        def __init__(self, data=None, signature=None):
            super().__init__(data or [])
            self.signature = signature

    class Struct(tuple):
        def __new__(cls, data=(), signature=None):
            instance = super().__new__(cls, tuple(data))
            instance.signature = signature
            return instance

    module.SessionBus = unittest.mock.MagicMock(name="dbus.SessionBus")
    module.DBusException = DBusException
    module.Interface = Interface
    module.Dictionary = Dictionary
    module.Array = Array
    module.Struct = Struct
    module.Byte = _scalar(int, "Byte")
    module.Boolean = _scalar(int, "Boolean")
    module.Int16 = _scalar(int, "Int16")
    module.Int32 = _scalar(int, "Int32")
    module.Int64 = _scalar(int, "Int64")
    module.UInt16 = _scalar(int, "UInt16")
    module.UInt32 = _scalar(int, "UInt32")
    module.UInt64 = _scalar(int, "UInt64")
    module.Double = _scalar(float, "Double")
    module.String = _scalar(str, "String")
    return module


def build_qtpy_module():
    """Builds a fresh ``qtpy`` package stand-in and registers its submodules."""
    qtpy = types.ModuleType("qtpy")
    qtpy.__path__ = []

    core = _fake_class_module("qtpy.QtCore")
    core.Qt = _QtNamespace("Qt")
    core.QEvent = _QtNamespace("QEvent")
    core.Signal = _Signal
    gui = _fake_class_module("qtpy.QtGui")
    widgets = _fake_class_module("qtpy.QtWidgets")

    qtpy.QtCore = core
    qtpy.QtGui = gui
    qtpy.QtWidgets = widgets

    sys.modules["qtpy"] = qtpy
    sys.modules["qtpy.QtCore"] = core
    sys.modules["qtpy.QtGui"] = gui
    sys.modules["qtpy.QtWidgets"] = widgets
    return qtpy


class _FakeGObject(metaclass=_FakeMeta):
    """Stateful stand-in base class for GTK 4 / Libadwaita / Gio objects."""

    _is_window_root = False

    def __init__(self, *args, **kwargs):
        self._children = []
        self._parent = None
        self._child = None
        self._extra_child = None
        self._signals = {}
        self._css_classes = set()
        self._visible = True
        self._sensitive = True
        self._active = bool(kwargs.get("active", False))
        self._selected = int(kwargs.get("selected", 0))
        self._value = float(kwargs.get("value", 0.0))
        self._text = str(kwargs.get("text", ""))
        self._label = str(kwargs.get("label", ""))
        self._title = str(kwargs.get("title", ""))
        self._subtitle = str(kwargs.get("subtitle", ""))
        self._description = str(kwargs.get("description", ""))
        self._heading = str(kwargs.get("heading", ""))
        self._body = str(kwargs.get("body", ""))
        self._tooltip_text = ""
        self._use_markup = True
        self._activatable = True
        self._expanded = False
        self._mapped = False
        self._closed = False
        self._model = None
        self._responses = {}
        self._response_enabled = {}
        self._toasts = []
        self._controllers = []
        self._pages = []
        self._page_meta = {}
        self._visible_child = None
        self.props = types.SimpleNamespace(active_window=None)

    def connect(self, signal_name, callback, *user_args):
        handlers = self._signals.setdefault(signal_name, [])
        handlers.append((callback, user_args))
        return len(handlers)

    def emit(self, signal_name, *args):
        result = False
        for callback, user_args in list(self._signals.get(signal_name, [])):
            res = callback(self, *args, *user_args)
            if res:
                result = res
        return result

    def append(self, widget):
        if widget is not None and widget not in self._children:
            self._children.append(widget)
            if hasattr(widget, "_parent"):
                widget._parent = self

    def add(self, widget):
        self.append(widget)

    def add_row(self, widget):
        self.append(widget)

    def add_prefix(self, widget):
        self.append(widget)

    def add_suffix(self, widget):
        self.append(widget)

    def remove(self, widget):
        if widget in self._children:
            self._children.remove(widget)
            if hasattr(widget, "_parent") and widget._parent is self:
                widget._parent = None

    def set_child(self, widget):
        self._child = widget
        if widget is not None and hasattr(widget, "_parent"):
            widget._parent = self

    def get_child(self):
        return self._child

    def set_content(self, widget):
        self.set_child(widget)

    def get_content(self):
        return self._child

    def set_extra_child(self, widget):
        self._extra_child = widget
        if widget is not None and hasattr(widget, "_parent"):
            widget._parent = self

    def get_extra_child(self):
        return self._extra_child

    def get_first_child(self):
        if self._children:
            return self._children[0]
        return self._child

    def get_next_sibling(self):
        parent = self._parent
        if parent is None:
            return None
        children = getattr(parent, "_children", [])
        if self in children:
            idx = children.index(self)
            if idx + 1 < len(children):
                return children[idx + 1]
        return None

    def get_parent(self):
        return self._parent

    def get_root(self):
        cur = self
        seen = set()
        while cur is not None and id(cur) not in seen:
            seen.add(id(cur))
            if getattr(cur, "_is_window_root", False):
                return cur
            cur = getattr(cur, "_parent", None)
        return None

    def add_controller(self, controller):
        self._controllers.append(controller)

    def add_css_class(self, name):
        self._css_classes.add(name)

    def remove_css_class(self, name):
        self._css_classes.discard(name)

    def has_css_class(self, name):
        return name in self._css_classes

    def set_visible(self, visible):
        self._visible = bool(visible)

    def get_visible(self):
        return self._visible

    def set_sensitive(self, sensitive):
        self._sensitive = bool(sensitive)

    def get_sensitive(self):
        return self._sensitive

    def set_active(self, active):
        val = bool(active)
        changed = val != self._active
        self._active = val
        if changed:
            self.emit("notify::active", None)

    def get_active(self):
        return self._active

    def set_selected(self, index):
        val = int(index)
        changed = val != self._selected
        self._selected = val
        if changed:
            self.emit("notify::selected", None)

    def get_selected(self):
        return self._selected

    def set_value(self, value):
        val = float(value)
        changed = val != self._value
        self._value = val
        if changed:
            self.emit("notify::value", None)

    def get_value(self):
        return self._value

    def set_text(self, text):
        val = str(text)
        changed = val != self._text
        self._text = val
        if changed:
            self.emit("changed")

    def get_text(self):
        return self._text

    def set_label(self, label):
        self._label = str(label)
        self._text = self._label

    def get_label(self):
        return self._label

    def set_title(self, title):
        self._title = str(title)

    def get_title(self):
        return self._title

    def set_subtitle(self, subtitle):
        self._subtitle = str(subtitle)

    def get_subtitle(self):
        return self._subtitle

    def set_description(self, description):
        self._description = str(description)

    def get_description(self):
        return self._description

    def set_tooltip_text(self, text):
        self._tooltip_text = "" if text is None else str(text)

    def get_tooltip_text(self):
        return self._tooltip_text

    def set_use_markup(self, use_markup):
        self._use_markup = bool(use_markup)

    def get_use_markup(self):
        return self._use_markup

    def set_activatable(self, activatable):
        self._activatable = bool(activatable)

    def get_activatable(self):
        return self._activatable

    def set_expanded(self, expanded):
        self._expanded = bool(expanded)

    def get_expanded(self):
        return self._expanded

    def set_model(self, model):
        self._model = model

    def get_model(self):
        return self._model

    def get_mapped(self):
        return self._mapped

    def present(self, _parent=None):
        self._mapped = True

    def close(self):
        self._mapped = False
        self._closed = True

    def add_response(self, response_id, label):
        self._responses[response_id] = label
        self._response_enabled.setdefault(response_id, True)

    def set_response_enabled(self, response_id, enabled):
        self._response_enabled[response_id] = bool(enabled)

    def get_response_enabled(self, response_id):
        return self._response_enabled.get(response_id, False)

    def add_toast(self, toast):
        self._toasts.append(toast)

    def popdown(self):
        self._visible = False

    def do_startup(self):
        pass

    def run(self, _argv=None):
        return 0

    def quit(self):
        pass

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        mock = unittest.mock.MagicMock()
        setattr(self, name, mock)
        return mock


class _FakeViewStackPage:
    """Minimal ``Adw.ViewStackPage`` stand-in."""

    def __init__(self, child, name, title, icon_name):
        self._child = child
        self._name = name
        self._title = title
        self._icon_name = icon_name

    def get_child(self):
        return self._child

    def get_name(self):
        return self._name

    def get_title(self):
        return self._title

    def get_icon_name(self):
        return self._icon_name


class _FakeListModel:
    """Minimal ``Gio.ListModel`` stand-in."""

    def __init__(self, items=None):
        self._items = list(items or [])

    def append(self, item):
        self._items.append(item)

    def get_n_items(self):
        return len(self._items)

    def get_item(self, index):
        return self._items[index] if 0 <= index < len(self._items) else None


class _FakeStringList(_FakeListModel):
    """Minimal ``Gtk.StringList`` stand-in."""

    @classmethod
    def new(cls, strings=None):
        return cls(strings or [])

    def get_string(self, index):
        item = self.get_item(index)
        return str(item) if item is not None else ""


class _FakeViewStack(_FakeGObject):
    """Minimal ``Adw.ViewStack`` stand-in tracking pages and visible child."""

    def add_titled_with_icon(self, child, name, title, icon_name):
        self.append(child)
        page = _FakeViewStackPage(child, name, title, icon_name)
        self._pages.append(page)
        self._page_meta[id(child)] = page
        if self._visible_child is None:
            self._visible_child = child
        return page

    def get_page(self, child):
        return self._page_meta.get(id(child))

    def get_pages(self):
        return _FakeListModel(self._pages)

    def get_visible_child(self):
        return self._visible_child

    def set_visible_child(self, child):
        if child is not self._visible_child:
            self._visible_child = child
            self.emit("notify::visible-child", None)

    def set_visible_child_name(self, name):
        for page in self._pages:
            if page.get_name() == name:
                self.set_visible_child(page.get_child())
                return


class _FakeApplicationWindow(_FakeGObject):
    """Root window stand-in returning itself from ``get_root()``."""

    _is_window_root = True


class _FakeSpinRow(_FakeGObject):
    """Minimal ``Adw.SpinRow`` stand-in supporting ``new_with_range``."""

    @classmethod
    def new_with_range(cls, minimum, maximum, step):
        row = cls()
        row._min = minimum
        row._max = maximum
        row._step = step
        row._value = float(minimum)
        return row


class _FakeImage(_FakeGObject):
    """Minimal ``Gtk.Image`` stand-in."""

    @classmethod
    def new_from_icon_name(cls, icon_name):
        img = cls()
        img._icon_name = icon_name
        return img

    @classmethod
    def new_from_file(cls, filename):
        img = cls()
        img._filename = filename
        return img


class _FakeToast(_FakeGObject):
    """Minimal ``Adw.Toast`` stand-in."""

    @classmethod
    def new(cls, title):
        toast = cls(title=title)
        return toast


class _FakeListStore(_FakeListModel):
    """Minimal ``Gio.ListStore`` stand-in."""

    @classmethod
    def new(cls, _item_type=None):
        return cls()


class _FakeTextBuffer(_FakeGObject):
    """Minimal ``Gtk.TextBuffer`` stand-in supporting iter slicing and cursor inserts."""

    def set_text(self, text, _length=-1):
        val = str(text)
        changed = val != self._text
        self._text = val
        if changed:
            self.emit("changed")

    def get_start_iter(self):
        return 0

    def get_end_iter(self):
        return len(self._text)

    def get_text(self, start=0, end=None, _include_hidden=True):
        return self._text[start:end]

    def insert_at_cursor(self, text, _length=-1):
        self._text = f"{self._text}{text}"
        self.emit("changed")


class _FakeTextView(_FakeGObject):
    """Minimal ``Gtk.TextView`` stand-in exposing a shared ``_FakeTextBuffer``."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._buffer = _FakeTextBuffer()

    def get_buffer(self):
        return self._buffer


def _fake_g_module(name, overrides=None):
    """Builds a module that fabricates subclassable ``_FakeGObject`` classes."""
    module = types.ModuleType(name)
    overrides = overrides or {}
    for attr, value in overrides.items():
        setattr(module, attr, value)

    def _fabricate(attr):
        if attr.startswith("__"):
            raise AttributeError(attr)
        fake = _FakeMeta(attr, (_FakeGObject,), {})
        setattr(module, attr, fake)
        return fake

    module.__getattr__ = _fabricate
    return module


_GDK_MODIFIER_FLAGS = {
    "SHIFT_MASK": 1 << 0,
    "LOCK_MASK": 1 << 1,
    "CONTROL_MASK": 1 << 2,
    "ALT_MASK": 1 << 3,
    "MOD1_MASK": 1 << 3,
    "SUPER_MASK": 1 << 26,
    "HYPER_MASK": 1 << 27,
    "META_MASK": 1 << 28,
}

_SEED_KEYVALS = {
    "space": 0x0020,
    "exclam": 0x0021,
    "quotedbl": 0x0022,
    "numbersign": 0x0023,
    "dollar": 0x0024,
    "percent": 0x0025,
    "ampersand": 0x0026,
    "apostrophe": 0x0027,
    "parenleft": 0x0028,
    "parenright": 0x0029,
    "asterisk": 0x002A,
    "plus": 0x002B,
    "comma": 0x002C,
    "minus": 0x002D,
    "period": 0x002E,
    "slash": 0x002F,
    "colon": 0x003A,
    "semicolon": 0x003B,
    "less": 0x003C,
    "equal": 0x003D,
    "greater": 0x003E,
    "question": 0x003F,
    "at": 0x0040,
    "bracketleft": 0x005B,
    "backslash": 0x005C,
    "bracketright": 0x005D,
    "asciicircum": 0x005E,
    "underscore": 0x005F,
    "grave": 0x0060,
    "braceleft": 0x007B,
    "bar": 0x007C,
    "braceright": 0x007D,
    "asciitilde": 0x007E,
    "BackSpace": 0xFF08,
    "Tab": 0xFF09,
    "Return": 0xFF0D,
    "Escape": 0xFF1B,
    "Delete": 0xFFFF,
    "ISO_Left_Tab": 0xFE20,
    "Shift_L": 0xFFE1,
    "Shift_R": 0xFFE2,
    "Control_L": 0xFFE3,
    "Control_R": 0xFFE4,
    "Meta_L": 0xFFE7,
    "Meta_R": 0xFFE8,
    "Alt_L": 0xFFE9,
    "Alt_R": 0xFFEA,
    "Super_L": 0xFFEB,
    "Super_R": 0xFFEC,
    "Hyper_L": 0xFFED,
    "Hyper_R": 0xFFEE,
}
for _ch in "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz":
    _SEED_KEYVALS[_ch] = ord(_ch)
for _fn in range(1, 13):
    _SEED_KEYVALS[f"F{_fn}"] = 0xFFBD + _fn


def build_gi_module():
    """Builds fresh ``gi`` and ``gi.repository`` stand-ins for GTK 4 / Adw."""
    gi = types.ModuleType("gi")
    gi.__path__ = []
    gi.versions = {}

    def require_version(namespace, version):
        gi.versions[namespace] = version

    gi.require_version = require_version

    repo = types.ModuleType("gi.repository")
    repo.__path__ = []

    keyval_to_name = {val: name for name, val in _SEED_KEYVALS.items()}
    next_dynamic_keyval = [0x1000000]

    class _GdkDisplay:
        @staticmethod
        def get_default():
            return None

    class _GdkModifierType:
        def __getattr__(self, name):
            if name.startswith("__"):
                raise AttributeError(name)
            val = _GDK_MODIFIER_FLAGS.get(name)
            if val is None:
                val = 1 << (zlib.crc32(name.encode()) % 24)
            setattr(self, name, val)
            return val

    gdk = types.ModuleType("gi.repository.Gdk")
    gdk.Display = _GdkDisplay
    gdk.ModifierType = _GdkModifierType()
    for _name, _val in _SEED_KEYVALS.items():
        setattr(gdk, f"KEY_{_name}", _val)

    def keyval_name(keyval):
        return keyval_to_name.get(keyval)

    def _gdk_getattr(attr):
        if attr.startswith("KEY_"):
            sym = attr[4:]
            val = next_dynamic_keyval[0]
            next_dynamic_keyval[0] += 1
            keyval_to_name[val] = sym
            setattr(gdk, attr, val)
            return val
        if attr.startswith("__"):
            raise AttributeError(attr)
        fake = _FakeMeta(attr, (_FakeGObject,), {})
        setattr(gdk, attr, fake)
        return fake

    gdk.keyval_name = keyval_name
    gdk.__getattr__ = _gdk_getattr

    idle_counter = [0]
    idle_sources = {}

    def markup_escape_text(text, length=-1):
        s = "" if text is None else str(text)
        if isinstance(length, int) and length >= 0:
            s = s[:length]
        return html.escape(s, quote=True).replace("&#x27;", "&apos;")

    def idle_add(func, *args):
        idle_counter[0] += 1
        sid = idle_counter[0]
        idle_sources[sid] = (func, args)
        return sid

    def source_remove(sid):
        return idle_sources.pop(sid, None) is not None

    class _FakeGError(Exception):
        """Stand-in for ``GLib.Error``."""

        def __init__(self, message="", domain=None, code=0):
            super().__init__(message)
            self.domain = domain
            self.code = code

    glib = types.ModuleType("gi.repository.GLib")
    glib.SOURCE_REMOVE = False
    glib.SOURCE_CONTINUE = True
    glib.PRIORITY_DEFAULT = 0
    glib.Error = _FakeGError
    glib.markup_escape_text = markup_escape_text
    glib.idle_add = idle_add
    glib.source_remove = source_remove
    glib.set_application_name = unittest.mock.MagicMock(name="GLib.set_application_name")
    glib.unix_signal_add = unittest.mock.MagicMock(name="GLib.unix_signal_add", return_value=1)
    glib._idle_sources = idle_sources

    glib_unix = types.ModuleType("gi.repository.GLibUnix")
    glib_unix.signal_add = unittest.mock.MagicMock(name="GLibUnix.signal_add", return_value=1)

    gio = _fake_g_module(
        "gi.repository.Gio",
        {
            "ApplicationFlags": _QtNamespace("Gio.ApplicationFlags"),
            "IOErrorEnum": _QtNamespace("Gio.IOErrorEnum"),
            "ListStore": _FakeListStore,
        },
    )
    gio.ApplicationFlags.DEFAULT_FLAGS = 0

    pango = _fake_g_module(
        "gi.repository.Pango",
        {
            "EllipsizeMode": _QtNamespace("Pango.EllipsizeMode"),
        },
    )

    gtk = _fake_g_module(
        "gi.repository.Gtk",
        {
            "Orientation": _QtNamespace("Gtk.Orientation"),
            "Align": _QtNamespace("Gtk.Align"),
            "SelectionMode": _QtNamespace("Gtk.SelectionMode"),
            "PropagationPhase": _QtNamespace("Gtk.PropagationPhase"),
            "PolicyType": _QtNamespace("Gtk.PolicyType"),
            "License": _QtNamespace("Gtk.License"),
            "DialogError": _QtNamespace("Gtk.DialogError"),
            "STYLE_PROVIDER_PRIORITY_APPLICATION": 600,
            "StringList": _FakeStringList,
            "Image": _FakeImage,
            "TextBuffer": _FakeTextBuffer,
            "TextView": _FakeTextView,
            "Window": _FakeApplicationWindow,
        },
    )

    adw = _fake_g_module(
        "gi.repository.Adw",
        {
            "ResponseAppearance": _QtNamespace("Adw.ResponseAppearance"),
            "ApplicationWindow": _FakeApplicationWindow,
            "ViewStack": _FakeViewStack,
            "SpinRow": _FakeSpinRow,
            "Toast": _FakeToast,
        },
    )

    repo.Adw = adw
    repo.Gdk = gdk
    repo.Gio = gio
    repo.GLib = glib
    repo.GLibUnix = glib_unix
    repo.Gtk = gtk
    repo.Pango = pango

    gi.repository = repo

    sys.modules["gi"] = gi
    sys.modules["gi.repository"] = repo
    sys.modules["gi.repository.Adw"] = adw
    sys.modules["gi.repository.Gdk"] = gdk
    sys.modules["gi.repository.Gio"] = gio
    sys.modules["gi.repository.GLib"] = glib
    sys.modules["gi.repository.GLibUnix"] = glib_unix
    sys.modules["gi.repository.Gtk"] = gtk
    sys.modules["gi.repository.Pango"] = pango
    return gi


def install():
    """Installs the stand-ins and puts settings-gui on ``sys.path`` (idempotent)."""
    global _installed
    if not _installed:
        sys.modules["dbus"] = build_dbus_module()
        build_qtpy_module()
        build_gi_module()
        _installed = True
    if str(SETTINGS_GUI_DIR) not in sys.path:
        sys.path.insert(0, str(SETTINGS_GUI_DIR))


def dbus_stub():
    """Returns the installed ``dbus`` stand-in module."""
    install()
    return sys.modules["dbus"]


def gi_stub():
    """Returns the installed ``gi`` stand-in module."""
    install()
    return sys.modules["gi"]


_installed = False

install()
