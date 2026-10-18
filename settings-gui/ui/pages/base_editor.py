# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Base class for Table-based editors (Macros, Keymap).
"""

from core.table_editor import compute_button_states, move_row_index, selected_rows
from qtpy.QtCore import Qt
from qtpy.QtWidgets import (
    QWidget,
)

from ui.helpers import notify_parent_changed


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
        notify_parent_changed(self)
        self.update_button_states()

    def find_row_by_key(self, key: str) -> int | None:
        """Finds row index for a given key. Returns None if not found."""
        for r in range(self.table.rowCount()):
            item = self.table.item(r, 0)
            if item and item.text() == key:
                return r
        return None

    def update_button_states(self):
        """Standard button state update logic."""
        if not self.table:
            return

        row = self.table.currentRow()
        count = self.table.rowCount()
        selected = len(self.table.selectedRanges()) > 0
        up_enabled, down_enabled, remove_enabled = compute_button_states(row, count, selected)

        if hasattr(self, "btn_up"):
            self.btn_up.setEnabled(up_enabled)
        if hasattr(self, "btn_down"):
            self.btn_down.setEnabled(down_enabled)
        if hasattr(self, "btn_remove"):
            self.btn_remove.setEnabled(remove_enabled)

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
        target = move_row_index(row, self.table.rowCount(), -1)
        if target is None:
            return
        self._swap_rows(row, target)
        self.table.selectRow(target)
        self.update_button_states()
        self._on_item_changed()

    def on_move_down(self):
        row = self.table.currentRow()
        target = move_row_index(row, self.table.rowCount(), 1)
        if target is None:
            return
        self._swap_rows(row, target)
        self.table.selectRow(target)
        self.update_button_states()
        self._on_item_changed()

    def on_remove(self):
        rows_to_delete = selected_rows(self.table.selectedRanges())
        if not rows_to_delete:
            return

        for row in reversed(rows_to_delete):
            self.table.removeRow(row)

        self.update_button_states()
        self._on_item_changed()
