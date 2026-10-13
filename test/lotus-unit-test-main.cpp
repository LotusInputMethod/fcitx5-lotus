/*
 * SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs every engine unit test group in one process each; ctest still owns
 * one entry per group, so labels, timeouts and crash isolation are unchanged.
 */
#include <array>
#include <cstdio>
#include <span>
#include <string_view>

int run_utf8();
int run_data();
int run_utils();
int run_encoder();
int run_rules_parser();
int run_flattener();
int run_spelling();
int run_engine_utils();
int run_engine_smoke();
int run_engine();
int run_rebuild();
int run_macro_dictionary();
int run_key_processor();
int run_golden_smoke();
int run_numeric_macro_case();

int main(int argc, char** argv) {
    struct Test {
        const char* name;
        int (*fn)();
    };
    const std::array tests{
        Test{.name = "utf8", .fn = run_utf8},
        Test{.name = "data", .fn = run_data},
        Test{.name = "utils", .fn = run_utils},
        Test{.name = "encoder", .fn = run_encoder},
        Test{.name = "rules_parser", .fn = run_rules_parser},
        Test{.name = "flattener", .fn = run_flattener},
        Test{.name = "spelling", .fn = run_spelling},
        Test{.name = "engine_utils", .fn = run_engine_utils},
        Test{.name = "engine_smoke", .fn = run_engine_smoke},
        Test{.name = "engine", .fn = run_engine},
        Test{.name = "rebuild", .fn = run_rebuild},
        Test{.name = "macro_dictionary", .fn = run_macro_dictionary},
        Test{.name = "key_processor", .fn = run_key_processor},
        Test{.name = "golden_smoke", .fn = run_golden_smoke},
        Test{.name = "numeric_macro_case", .fn = run_numeric_macro_case},
    };

    const std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 3 || std::string_view(args[1]) != "--run") {
        std::fprintf(stderr, "usage: %s --run <test>\navailable:", args[0]);
        for (const auto& t : tests) {
            std::fprintf(stderr, " %s", t.name);
        }
        std::fprintf(stderr, "\n");
        return 2;
    }
    const std::string_view wanted(args[2]);
    for (const auto& t : tests) {
        if (wanted == t.name) {
            return t.fn();
        }
    }
    std::fprintf(stderr, "unknown test: %s\n", args[2]);
    return 2;
}
