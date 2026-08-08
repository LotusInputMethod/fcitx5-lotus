import sys
from PyQt6.QtWidgets import QApplication
from ui.main_window import LotusSettingsWindow


def main():
    app = QApplication(sys.argv)

    app.setStyleSheet("""
        QLabel#CategoryTitle { font-size: 22px; }
        QLabel#AboutTitle { font-size: 26px; }
        QLabel#KeyCap {
            background-color: palette(button);
            color: palette(button-text);
            border: 1px solid palette(mid);
            border-bottom: 2px solid palette(dark);
            border-radius: 4px;
            padding: 2px 6px;
            font-weight: bold;
            font-family: monospace;
        }
        QLabel#ShortcutWarning {
            color: palette(link-visited);
            font-size: 12px;
        }
    """)

    window = LotusSettingsWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
