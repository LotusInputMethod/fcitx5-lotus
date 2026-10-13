/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file transformation.h
 * @brief One node of the engine composition, 1:1 with Go's Transformation.
 *
 * Go keeps `composition []*Transformation` with `Target *Transformation` back references and compares
 * nodes by pointer identity, while leaning on the garbage collector to reclaim them. The port mirrors
 * that with std::shared_ptr: `Target` never points forward, so reference counting reclaims nodes exactly
 * when the last list referencing them goes away, and shared_ptr equality is pointer equality.
 */

#ifndef _LOTUS_TRANSFORMATION_H_
#define _LOTUS_TRANSFORMATION_H_

#include "rule.h"

#include <memory>

namespace lotus {

    struct Transformation;

    /** @brief Shared ownership of a composition node, 1:1 with Go's *Transformation. */
    using TransformationPtr = std::shared_ptr<Transformation>;

    /** @brief One node of the composition, 1:1 with Go's Transformation. */
    struct Transformation {
        Rule              rule;                ///< Rule that produced the node
        TransformationPtr target;              ///< Node the effect applies to, null for an appending node
        bool              isUpperCase = false; ///< Key was typed with a capital letter
    };

    /**
     * @brief Builds an appending node for a key, like Go's newAppendingTrans.
     * @param key Input key.
     * @param isUpperCase Whether the key was uppercase.
     * @return The new node.
     */
    TransformationPtr makeAppendingTrans(char32_t key, bool isUpperCase);

    /**
     * @brief Compares two nodes by identity, like Go's pointer comparison (null equals null).
     * @param first First node.
     * @param second Second node.
     * @return True when both refer to the same node, or when both are null.
     */
    bool isSame(const TransformationPtr& first, const TransformationPtr& second);

} // namespace lotus

#endif // _LOTUS_TRANSFORMATION_H_
