#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Application entry point.
"""

import signal
import sys

from i18n import setup_i18n
from qtpy.QtCore import QTimer
from qtpy.QtGui import QIcon
from qtpy.QtWidgets import QApplication
from ui.main_window import LotusSettingsWindow


def main():
    """Main execution function."""
    setup_i18n()
    app = QApplication(sys.argv)
    app.setDesktopFileName("org.fcitx.Fcitx5.Addon.Lotus.Settings")
    app.setApplicationName("org.fcitx.Fcitx5.Addon.Lotus.Settings")
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    app.setWindowIcon(QIcon.fromTheme("fcitx-lotus"))

    timer = QTimer()
    timer.start(500)
    timer.timeout.connect(lambda: None)

    window = LotusSettingsWindow()
    window.show()

    try:
        sys.exit(app.exec())
    except KeyboardInterrupt:
        app.quit()


if __name__ == "__main__":
    main()
