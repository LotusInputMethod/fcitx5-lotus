// SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

function syncByKey(model, items, keyFn, mapFn) {
    const list = items || [];
    const targetKeys = Object.create(null);
    for (let i = 0; i < list.length; i++)
        targetKeys[keyFn(list[i])] = true;

    for (let i = model.count - 1; i >= 0; i--) {
        if (!targetKeys[model.get(i).itemKey])
            model.remove(i);
    }

    for (let j = 0; j < list.length; j++) {
        const key = keyFn(list[j]);
        const mapped = mapFn(list[j]);
        mapped.itemKey = key;

        if (j < model.count && model.get(j).itemKey === key) {
            const existing = model.get(j);
            let changed = false;
            for (const prop in mapped) {
                if (existing[prop] !== mapped[prop]) {
                    changed = true;
                    break;
                }
            }
            if (changed)
                model.set(j, mapped);
        } else {
            let foundAt = -1;
            for (let k = j + 1; k < model.count; k++) {
                if (model.get(k).itemKey === key) {
                    foundAt = k;
                    break;
                }
            }
            if (foundAt !== -1) {
                model.move(foundAt, j, 1);
                model.set(j, mapped);
            } else {
                model.insert(j, mapped);
            }
        }
    }

    while (model.count > list.length)
        model.remove(model.count - 1);
}
