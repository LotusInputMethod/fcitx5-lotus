/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * @file spelling.cpp
 * @brief Implementation of the Vietnamese orthography validation.
 */
#include "spelling.h"

#include "data/spelling_data.h"
#include "utils.h"

#include <cstddef>

namespace lotus {

    std::vector<int> lookup(const std::vector<std::vector<std::u32string_view>>& sequences, std::u32string_view input, bool inputIsFull, bool inputIsComplete) {
        std::vector<int>  result;
        const std::size_t inputLength = input.size();
        for (std::size_t index = 0; index < sequences.size(); ++index) {
            for (const auto& token : sequences[index]) {
                if (token.size() < inputLength || (inputIsFull && token.size() > inputLength)) {
                    continue;
                }
                bool isMatch = true;
                for (std::size_t k = 0; k < inputLength; ++k) {
                    if (input[k] != token[k] && (inputIsComplete || utils::addMarkToTonelessChar(token[k], Mark::None) != input[k])) {
                        isMatch = false;
                        break;
                    }
                }
                if (isMatch) {
                    result.push_back(static_cast<int>(index));
                    break;
                }
            }
        }
        return result;
    }

    bool isValidVC(const std::vector<int>& vowelIndexes, const std::vector<int>& lastConsonantIndexes) {
        const std::vector<std::vector<int>>& matrix = data::vcMatrix();
        for (int vowel : vowelIndexes) {
            if (vowel < 0 || static_cast<std::size_t>(vowel) >= matrix.size()) {
                continue;
            }
            for (int allowed : matrix[static_cast<std::size_t>(vowel)]) {
                for (int consonant : lastConsonantIndexes) {
                    if (allowed == consonant) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool isValidCVC(std::u32string_view firstConsonant, std::u32string_view vowel, std::u32string_view lastConsonant, bool inputIsFullComplete) {
        std::vector<int> firstConsonantIndexes;
        std::vector<int> vowelIndexes;
        std::vector<int> lastConsonantIndexes;

        if (!firstConsonant.empty()) {
            firstConsonantIndexes = lookup(data::firstConsonantSeqs(), firstConsonant, inputIsFullComplete || !vowel.empty(), true);
            if (firstConsonantIndexes.empty()) {
                return false;
            }
        }
        if (!vowel.empty()) {
            vowelIndexes = lookup(data::vowelSeqs(), vowel, inputIsFullComplete || !lastConsonant.empty(), inputIsFullComplete);
            if (vowelIndexes.empty()) {
                return false;
            }
        }
        if (!lastConsonant.empty()) {
            lastConsonantIndexes = lookup(data::lastConsonantSeqs(), lastConsonant, inputIsFullComplete, true);
            if (lastConsonantIndexes.empty()) {
                return false;
            }
        }
        if (vowelIndexes.empty()) {
            // First consonant only.
            return !firstConsonantIndexes.empty();
        }
        if (!lastConsonantIndexes.empty()) {
            // Vowel + last consonant.
            return isValidVC(vowelIndexes, lastConsonantIndexes);
        }
        // Vowel only.
        return true;
    }

} // namespace lotus
