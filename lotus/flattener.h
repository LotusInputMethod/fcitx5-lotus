/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file flattener.h
 * @brief Renders a composition into code points, ported 1:1 from bamboo-core/flattener.go.
 */

#ifndef _LOTUS_FLATTENER_H_
#define _LOTUS_FLATTENER_H_

#include "composition.h"

#include <cstddef>
#include <string>
#include <vector>

namespace lotus {

    /** @brief A rendered composition plus the composition index of every rendered code point. */
    struct Canvas {
        std::u32string           text;  ///< One code point per contributing node, in composition order
        std::vector<std::size_t> nodes; ///< Composition index of each entry of text
    };

    /**
     * @brief Renders a composition and reports which node produced each code point, like flatten().
     * @param composition Composition.
     * @param mode Flattening mode (English, ToneLess, MarkLess, LowerCase, ...).
     * @return Rendered code points with the composition index of each of them.
     */
    Canvas renderCanvas(const Composition& composition, ModeSet mode);

    /**
     * @brief Renders a composition, recording the node of every entry when nodes is not null.
     * @param composition Composition.
     * @param mode Flattening mode.
     * @param nodes Destination of the node indexes, or null when the caller does not need them.
     * @return Rendered code points.
     */
    std::u32string renderInto(const Composition& composition, ModeSet mode, std::vector<std::size_t>* nodes);

    /**
     * @brief Renders a composition into code points, like Go's getCanvas / Flatten.
     * @param composition Composition.
     * @param mode Flattening mode (English, ToneLess, MarkLess, LowerCase, ...).
     * @return Rendered code points. Go's Flatten would then encode this canvas to UTF-8.
     */
    std::u32string flatten(const Composition& composition, ModeSet mode);

} // namespace lotus

#endif // _LOTUS_FLATTENER_H_
