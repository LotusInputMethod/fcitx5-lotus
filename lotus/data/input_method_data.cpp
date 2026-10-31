/*
 * SPDX-FileCopyrightText: 2018 Luong Thanh Lam <ltlam93@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "data/input_method_data.h"

namespace lotus::data {

    static_assert(static_cast<std::size_t>(InputMethodId::Custom) + 1U == kInputMethodCount);

    const std::array<std::unordered_map<std::string, std::string>, kInputMethodCount> kInputMethodDefinitions = {
        {
            {

                {"a", "A_Â"},
                {"d", "D_Đ"},
                {"e", "E_Ê"},
                {"f", "DauHuyen"},
                {"j", "DauNang"},
                {"o", "O_Ô"},
                {"r", "DauHoi"},
                {"s", "DauSac"},
                {"w", "UOA_ƯƠĂ"},
                {"x", "DauNga"},
                {"z", "XoaDauThanh"},
            },
            {

                {"0", "XoaDauThanh"},
                {"1", "DauSac"},
                {"2", "DauHuyen"},
                {"3", "DauHoi"},
                {"4", "DauNga"},
                {"5", "DauNang"},
                {"6", "AEO_ÂÊÔ"},
                {"7", "UO_ƯƠ"},
                {"8", "A_Ă"},
                {"9", "D_Đ"},
            },
            {

                {"0", "XoaDauThanh"}, {"1", "DauSac"}, {"2", "DauHuyen"}, {"3", "DauHoi"}, {"4", "DauNga"},  {"5", "DauNang"}, {"6", "AEO_ÂÊÔ"},
                {"7", "UO_ƯƠ"},       {"8", "A_Ă"},    {"9", "D_Đ"},      {"a", "A_Â"},    {"d", "D_Đ"},     {"e", "E_Ê"},     {"f", "DauHuyen"},
                {"j", "DauNang"},     {"o", "O_Ô"},    {"r", "DauHoi"},   {"s", "DauSac"}, {"w", "UOA_ƯƠĂ"}, {"x", "DauNga"},  {"z", "XoaDauThanh"},
            },
            {

                {"\'", "DauSac"}, {"(", "A_Ă"},     {"*", "UO_ƯƠ"},    {"+", "UO_ƯƠ"},   {".", "DauNang"}, {"0", "XoaDauThanh"}, {"1", "DauSac"},   {"2", "DauHuyen"},
                {"3", "DauHoi"},  {"4", "DauNga"},  {"5", "DauNang"},  {"6", "AEO_ÂÊÔ"}, {"7", "UO_ƯƠ"},   {"8", "A_Ă"},         {"9", "D_Đ"},      {"?", "DauHoi"},
                {"\\", "D_Đ"},    {"^", "AEO_ÂÊÔ"}, {"`", "DauHuyen"}, {"a", "A_Â"},     {"d", "D_Đ"},     {"e", "E_Ê"},         {"f", "DauHuyen"}, {"j", "DauNang"},
                {"o", "O_Ô"},     {"r", "DauHoi"},  {"s", "DauSac"},   {"w", "UOA_ƯƠĂ"}, {"x", "DauNga"},  {"z", "XoaDauThanh"}, {"~", "DauNga"},
            },
            {

                {"\'", "DauSac"},
                {"(", "A_Ă"},
                {"*", "UO_ƯƠ"},
                {"+", "UO_ƯƠ"},
                {".", "DauNang"},
                {"0", "XoaDauThanh"},
                {"?", "DauHoi"},
                {"^", "AEO_ÂÊÔ"},
                {"`", "DauHuyen"},
                {"d", "D_Đ"},
                {"~", "DauNga"},
            },
            {

                {"!", "_Ă"},
                {"#", "_Ê"},
                {"$", "_Ô"},
                {")", "_Đ"},
                {"0", "__đ"},
                {"1", "__ă"},
                {"2", "__â"},
                {"3", "__ê"},
                {"4", "__ô"},
                {"5", "DauHuyen"},
                {"6", "DauHoi"},
                {"7", "DauNga"},
                {"8", "DauSac"},
                {"9", "DauNang"},
                {"@", "_Â"},
            },
            {

                {"\"", "DauHuyen"},
                {"&", "XoaDauThanh"},
                {"\'", "DauHoi"},
                {"(", "DauNga"},
                {"-", "DauNang"},
                {"_", "UO_ƯƠ"},
                {"à", "D_Đ"},
                {"ç", "A_Ă"},
                {"è", "AEO_ÂÊÔ"},
                {"é", "DauSac"},
            },
            // Custom has no built-in definition.
            {},
        },
    };

} // namespace lotus::data
