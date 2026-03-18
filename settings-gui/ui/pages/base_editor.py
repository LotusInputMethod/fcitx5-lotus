# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Base class for Table-based editors (Macros, Keymap).
"""

from PySide6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QTableWidget,
    QTableWidgetItem,
    QHeaderView,
    QMessageBox,
    QLabel,
    QSizePolicy,
    QAbstractItemView,
    QFileDialog,
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon
from i18n import _


class BaseEditorPage(QWidget):
    """Base UI for table-based editors."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.table = None

    def apply_table_style(self):
        """Applies modern styling to the table and connects signals."""
        if not self.table:
            return

        self.table.setFocusPolicy(Qt.NoFocus)
        self.table.verticalHeader().setVisible(False)
        self.table.setShowGrid(False)
        self.table.itemSelectionChanged.connect(self.update_button_states)

        self.table.setStyleSheet(
            """
            QTableWidget {
                border: 1px solid palette(midlight);
                border-radius: 6px;
                background-color: transparent;
            }
            QTableWidget::item {
                padding: 4px;
                border-bottom: 1px solid palette(midlight);
            }
            QTableWidget::item:selected {
                background-color: palette(highlight);
                color: palette(highlighted-text);
            }
            QHeaderView::section {
                background-color: transparent;
                border: none;
                border-bottom: 2px solid palette(mid);
                padding: 4px;
                font-weight: bold;
            }
        """
        )

    def _on_item_changed(self):
        """Notifies parent window of change."""
        main_win = self.window()
        if hasattr(main_win, "on_changed"):
            main_win.on_changed()
        self.update_button_states()

    def update_button_states(self):
        """Standard button state update logic."""
        if not self.table:
            return

        row = self.table.currentRow()
        count = self.table.rowCount()
        selected = len(self.table.selectedRanges()) > 0

        if hasattr(self, "btn_up"):
            self.btn_up.setEnabled(row > 0)
        if hasattr(self, "btn_down"):
            self.btn_down.setEnabled(0 <= row < count - 1)
        if hasattr(self, "btn_remove"):
            self.btn_remove.setEnabled(selected)

    def _swap_rows(self, row1, row2):
        """Swaps two rows in the table, preserving widgets if any."""
        for col in range(self.table.columnCount()):
            # Swap Items
            item1 = self.table.takeItem(row1, col)
            item2 = self.table.takeItem(row2, col)
            self.table.setItem(row1, col, item2)
            self.table.setItem(row2, col, item1)

            # Swap Widgets
            w1 = self.table.cellWidget(row1, col)
            w2 = self.table.cellWidget(row2, col)

            if w1:
                self.table.removeCellWidget(row1, col)
            if w2:
                self.table.removeCellWidget(row2, col)

            if w1:
                self.table.setCellWidget(row2, col, w1)
            if w2:
                self.table.setCellWidget(row1, col, w2)

    def on_move_up(self):
        row = self.table.currentRow()
        if row <= 0:
            return
        self._swap_rows(row, row - 1)
        self.table.selectRow(row - 1)
        self.update_button_states()
        self._on_item_changed()

    def on_move_down(self):
        row = self.table.currentRow()
        if row < 0 or row >= self.table.rowCount() - 1:
            return
        self._swap_rows(row, row + 1)
        self.table.selectRow(row + 1)
        self.update_button_states()
        self._on_item_changed()

    def on_remove(self):
        selected_ranges = self.table.selectedRanges()
        if not selected_ranges:
            return

        rows_to_delete = set()
        for r in selected_ranges:
            for i in range(r.topRow(), r.bottomRow() + 1):
                rows_to_delete.add(i)

        for row in sorted(list(rows_to_delete), reverse=True):
            self.table.removeRow(row)

        self.update_button_states()
        self._on_item_changed()
