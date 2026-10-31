/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "data/case_data.h"

#include <algorithm>
#include <string_view>

namespace lotus::data {
    namespace {
        constexpr auto                kLowerUpperData = std::to_array<std::pair<char32_t, char32_t>>({
            {U'à', U'À'}, {U'á', U'Á'}, {U'â', U'Â'}, {U'ã', U'Ã'}, {U'ä', U'Ä'}, {U'å', U'Å'}, {U'æ', U'Æ'}, {U'ç', U'Ç'}, {U'è', U'È'}, {U'é', U'É'}, {U'ê', U'Ê'}, {U'ë', U'Ë'},
            {U'ì', U'Ì'}, {U'í', U'Í'}, {U'î', U'Î'}, {U'ï', U'Ï'}, {U'ð', U'Ð'}, {U'ñ', U'Ñ'}, {U'ò', U'Ò'}, {U'ó', U'Ó'}, {U'ô', U'Ô'}, {U'õ', U'Õ'}, {U'ö', U'Ö'}, {U'ø', U'Ø'},
            {U'ù', U'Ù'}, {U'ú', U'Ú'}, {U'û', U'Û'}, {U'ü', U'Ü'}, {U'ý', U'Ý'}, {U'þ', U'Þ'}, {U'ÿ', U'Ÿ'}, {U'ă', U'Ă'}, {U'đ', U'Đ'}, {U'ĩ', U'Ĩ'}, {U'ũ', U'Ũ'}, {U'ơ', U'Ơ'},
            {U'ư', U'Ư'}, {U'ạ', U'Ạ'}, {U'ả', U'Ả'}, {U'ấ', U'Ấ'}, {U'ầ', U'Ầ'}, {U'ẩ', U'Ẩ'}, {U'ẫ', U'Ẫ'}, {U'ậ', U'Ậ'}, {U'ắ', U'Ắ'}, {U'ằ', U'Ằ'}, {U'ẳ', U'Ẳ'}, {U'ẵ', U'Ẵ'},
            {U'ặ', U'Ặ'}, {U'ẹ', U'Ẹ'}, {U'ẻ', U'Ẻ'}, {U'ẽ', U'Ẽ'}, {U'ế', U'Ế'}, {U'ề', U'Ề'}, {U'ể', U'Ể'}, {U'ễ', U'Ễ'}, {U'ệ', U'Ệ'}, {U'ỉ', U'Ỉ'}, {U'ị', U'Ị'}, {U'ọ', U'Ọ'},
            {U'ỏ', U'Ỏ'}, {U'ố', U'Ố'}, {U'ồ', U'Ồ'}, {U'ổ', U'Ổ'}, {U'ỗ', U'Ỗ'}, {U'ộ', U'Ộ'}, {U'ớ', U'Ớ'}, {U'ờ', U'Ờ'}, {U'ở', U'Ở'}, {U'ỡ', U'Ỡ'}, {U'ợ', U'Ợ'}, {U'ụ', U'Ụ'},
            {U'ủ', U'Ủ'}, {U'ứ', U'Ứ'}, {U'ừ', U'Ừ'}, {U'ử', U'Ử'}, {U'ữ', U'Ữ'}, {U'ự', U'Ự'}, {U'ỳ', U'Ỳ'}, {U'ỵ', U'Ỵ'}, {U'ỷ', U'Ỷ'}, {U'ỹ', U'Ỹ'},
        });
        constexpr auto                kUpperLowerData = std::to_array<std::pair<char32_t, char32_t>>({
            {U'À', U'à'}, {U'Á', U'á'}, {U'Â', U'â'}, {U'Ã', U'ã'}, {U'Ä', U'ä'}, {U'Å', U'å'}, {U'Æ', U'æ'}, {U'Ç', U'ç'}, {U'È', U'è'}, {U'É', U'é'}, {U'Ê', U'ê'}, {U'Ë', U'ë'},
            {U'Ì', U'ì'}, {U'Í', U'í'}, {U'Î', U'î'}, {U'Ï', U'ï'}, {U'Ð', U'ð'}, {U'Ñ', U'ñ'}, {U'Ò', U'ò'}, {U'Ó', U'ó'}, {U'Ô', U'ô'}, {U'Õ', U'õ'}, {U'Ö', U'ö'}, {U'Ø', U'ø'},
            {U'Ù', U'ù'}, {U'Ú', U'ú'}, {U'Û', U'û'}, {U'Ü', U'ü'}, {U'Ý', U'ý'}, {U'Þ', U'þ'}, {U'Ă', U'ă'}, {U'Đ', U'đ'}, {U'Ĩ', U'ĩ'}, {U'Ũ', U'ũ'}, {U'Ÿ', U'ÿ'}, {U'Ơ', U'ơ'},
            {U'Ư', U'ư'}, {U'Ạ', U'ạ'}, {U'Ả', U'ả'}, {U'Ấ', U'ấ'}, {U'Ầ', U'ầ'}, {U'Ẩ', U'ẩ'}, {U'Ẫ', U'ẫ'}, {U'Ậ', U'ậ'}, {U'Ắ', U'ắ'}, {U'Ằ', U'ằ'}, {U'Ẳ', U'ẳ'}, {U'Ẵ', U'ẵ'},
            {U'Ặ', U'ặ'}, {U'Ẹ', U'ẹ'}, {U'Ẻ', U'ẻ'}, {U'Ẽ', U'ẽ'}, {U'Ế', U'ế'}, {U'Ề', U'ề'}, {U'Ể', U'ể'}, {U'Ễ', U'ễ'}, {U'Ệ', U'ệ'}, {U'Ỉ', U'ỉ'}, {U'Ị', U'ị'}, {U'Ọ', U'ọ'},
            {U'Ỏ', U'ỏ'}, {U'Ố', U'ố'}, {U'Ồ', U'ồ'}, {U'Ổ', U'ổ'}, {U'Ỗ', U'ỗ'}, {U'Ộ', U'ộ'}, {U'Ớ', U'ớ'}, {U'Ờ', U'ờ'}, {U'Ở', U'ở'}, {U'Ỡ', U'ỡ'}, {U'Ợ', U'ợ'}, {U'Ụ', U'ụ'},
            {U'Ủ', U'ủ'}, {U'Ứ', U'ứ'}, {U'Ừ', U'ừ'}, {U'Ử', U'ử'}, {U'Ữ', U'ữ'}, {U'Ự', U'ự'}, {U'Ỳ', U'ỳ'}, {U'Ỵ', U'ỵ'}, {U'Ỷ', U'ỷ'}, {U'Ỹ', U'ỹ'},
        });

        constexpr std::u32string_view kLettersView = U"ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞßàáâãäåæçèéêëìíîïðñòóôõöøùúûüýþÿĂăĐđĨĩŨũŸƠơƯưẠạẢảẤấẦầẨẩẪẫẬậẮắẰằẲẳẴẵẶặẸẹẺẻẼẽẾếỀềỂ"
                                                     "ểỄễỆệỈỉỊịỌọỎỏỐốỒồỔổỖỗỘộỚớỜờỞởỠỡỢợỤụỦủỨứỪừỬửỮữỰựỲỳỴỵỶỷỸỹ";
    } // namespace

    constinit const std::span<const std::pair<char32_t, char32_t>> kLowerUpperPairs{kLowerUpperData};
    constinit const std::span<const std::pair<char32_t, char32_t>> kUpperLowerPairs{kUpperLowerData};
    constinit const std::span<const char32_t>                      kLetters{kLettersView};

    static_assert(std::ranges::is_sorted(kLowerUpperData, {}, &std::pair<char32_t, char32_t>::first));
    static_assert(std::ranges::is_sorted(kUpperLowerData, {}, &std::pair<char32_t, char32_t>::first));
    static_assert(std::ranges::is_sorted(kLettersView));

} // namespace lotus::data
