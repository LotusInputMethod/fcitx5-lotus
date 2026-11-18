// SPDX-License-Identifier: GPL-3.0-or-later
//
// The server never holds a Shift of its own except for the in-flight selection it pressed itself,
// and a physically held Shift must turn a Select request into the Backspace fallback: a Shift+Left
// selection would fight the user's own modifier.
#include "lotus-physical-shift.h"
#include "test-utils.h"

#include <array>
#include <cstdio>

namespace {

    int  failures = 0;

    void check(const char* name, bool condition) {
        if (!condition) {
            reportFailure(name, "holds", "did not hold", "the server must fail safe to Backspace whenever the physical Shift state is not certainly Released");
            ++failures;
        }
    }

} // namespace

int main() {
    // Unknown Shift fails safe to Backspace: never emit Shift+Left against an unknown modifier.
    check("Select with Held Shift falls back to Backspace", resolveKeyboardOp(KbOp::Select, ShiftState::Held) == KbOp::Backspace);
    check("Select with Unknown Shift falls back to Backspace", resolveKeyboardOp(KbOp::Select, ShiftState::Unknown) == KbOp::Backspace);
    check("Select with Released Shift stays Select", resolveKeyboardOp(KbOp::Select, ShiftState::Released) == KbOp::Select);
    check("Backspace with Held Shift is unchanged", resolveKeyboardOp(KbOp::Backspace, ShiftState::Held) == KbOp::Backspace);
    check("Backspace with Unknown Shift is unchanged", resolveKeyboardOp(KbOp::Backspace, ShiftState::Unknown) == KbOp::Backspace);
    check("Backspace with Released Shift is unchanged", resolveKeyboardOp(KbOp::Backspace, ShiftState::Released) == KbOp::Backspace);

    // Decoding one EVIOCGKEY answer.
    std::array<unsigned char, (KEY_MAX + 7U) / 8U> bits{};
    check("empty bitmap", !keyBitSet(bits, KEY_LEFTSHIFT) && !keyBitSet(bits, KEY_RIGHTSHIFT));
    bits[KEY_LEFTSHIFT / 8U] |= static_cast<unsigned char>(1U << (KEY_LEFTSHIFT % 8U));
    check("left bit only", keyBitSet(bits, KEY_LEFTSHIFT) && !keyBitSet(bits, KEY_RIGHTSHIFT));
    bits[KEY_LEFTSHIFT / 8U] = 0;
    bits[KEY_RIGHTSHIFT / 8U] |= static_cast<unsigned char>(1U << (KEY_RIGHTSHIFT % 8U));
    check("right bit only", keyBitSet(bits, KEY_RIGHTSHIFT) && !keyBitSet(bits, KEY_LEFTSHIFT));
    {
        const std::array<ShiftState, 0> empty{};
        check("merge empty with no untracked is Unknown", mergeShiftStates(empty, false) == ShiftState::Unknown);
        check("merge empty with untracked is Unknown", mergeShiftStates(empty, true) == ShiftState::Unknown);
    }
    {
        const std::array<ShiftState, 2> allReleased{ShiftState::Released, ShiftState::Released};
        check("merge all Released with no untracked is Released", mergeShiftStates(allReleased, false) == ShiftState::Released);
    }
    {
        const std::array<ShiftState, 3> unknownAmongReleased{ShiftState::Released, ShiftState::Unknown, ShiftState::Released};
        check("merge Unknown among Released with no untracked is Unknown", mergeShiftStates(unknownAmongReleased, false) == ShiftState::Unknown);
    }
    {
        const std::array<ShiftState, 3> heldUnknownReleased{ShiftState::Held, ShiftState::Unknown, ShiftState::Released};
        check("merge Held with Unknown and Released with no untracked is Held", mergeShiftStates(heldUnknownReleased, false) == ShiftState::Held);
    }
    {
        const std::array<ShiftState, 2> allReleased{ShiftState::Released, ShiftState::Released};
        check("merge all Released with untracked poisons to Unknown", mergeShiftStates(allReleased, true) == ShiftState::Unknown);
    }
    {
        const std::array<ShiftState, 1> singleHeld{ShiftState::Held};
        check("merge single Held with no untracked is Held", mergeShiftStates(singleHeld, false) == ShiftState::Held);
        const std::array<ShiftState, 1> singleUnknown{ShiftState::Unknown};
        check("merge single Unknown with no untracked is Unknown", mergeShiftStates(singleUnknown, false) == ShiftState::Unknown);
    }
    {
        const std::array<ShiftState, 2> heldReleased{ShiftState::Held, ShiftState::Released};
        check("merge Held with untracked is still Held", mergeShiftStates(heldReleased, true) == ShiftState::Held);
    }
    {
        const std::array<ShiftState, 1> singleHeldUntracked{ShiftState::Held};
        check("merge single Held with untracked is still Held", mergeShiftStates(singleHeldUntracked, true) == ShiftState::Held);
    }

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lotus select shift plan: all checks passed\n");
    return 0;
}
