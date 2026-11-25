# Quy tắc code style

Tài liệu này là nguồn duy nhất cho quy tắc code của fcitx5-lotus. Khi có xung đột, tài liệu này thắng.

## 1. Format & lint (bắt buộc)

- C/C++: `clang-format` theo `.clang-format`; `clang-tidy` theo `.clang-tidy`, **0 cảnh báo trong code của repo**.
- `NOLINT` phải ghi rõ check và lý do, ví dụ `// NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)`.
- QML (`settings-gui`): `qmllint`, tương thích Qt 6.2+.

## 2. Chuẩn ngôn ngữ

- C++20 (`CMAKE_CXX_STANDARD 20`), không hạ chuẩn.
- Dùng thư viện chuẩn: `std::string_view`, `std::span`, `std::ranges`, `std::erase_if`, `std::optional`, `std::cmp_*`, designated initializer.
- Không viết lại thứ thư viện chuẩn đã có. Không dùng `std::regex` trong engine (`lotus/`, phần `src/` nạp vào engine): nhị phân phình và không cần thiết.

## 3. RAII & quyền sở hữu

- Mọi tài nguyên phải có chủ sở hữu RAII: fd dùng `FdGuard`, handle/thừa kế C dùng `std::unique_ptr` + deleter (`LibevdevDevice`, `LibevdevUinput`, `LibinputEvent`, `UdevDeleter`…).
- Không để fd thô sống qua nhiều câu lệnh rồi `close()` tay; bọc ngay khi nhận (`FdGuard fd(open(...));`), kể cả trên nhánh lỗi.
- Không `malloc`/`free`/`strdup`. `new`/`delete` chỉ được phép ở factory của fcitx5 (kèm `NOLINT`).
- Class giữ tài nguyên: Rule of five tường minh (xoá copy, khai báo move); không viết destructor tay chỉ để `close()`/`free()`.

## 4. Con trỏ & tham chiếu

- Con trỏ thô chỉ dùng khi: (a) giá trị có thể null, (b) API C, (c) chữ ký do fcitx5/libinput áp (virtual override, callback, factory).
- Mượn non-null, không đổi đích ⇒ dùng reference (ví dụ `LotusEngine& engine_`).
- Không truyền con trỏ khi reference đủ; không nhận tham số bằng value nếu chỉ đọc.

## 5. Chuỗi

- Tham số/ô dữ liệu chỉ đọc: `std::string_view`; text đã decode: `std::u32string_view`; chỉ dùng `std::string` khi thực sự sở hữu/biến đổi.
- Giữ `const char*` khi hai đầu là API C (`open`, `getenv`, `printf`, `popen`) hoặc khi API fcitx5 nhận `const std::string&` — đổi kiểu ở đó chỉ tạo `std::string(...)` thừa.
- `test/` được giữ `const char*` trong helper `check(name, ...)` vì đi kèm `std::printf("%s")`.

## 6. Cấu trúc file & header

- Mỗi `.h` phải có `.cpp` cùng tên. Ngoại lệ: header chỉ chứa khai báo kiểu/`constexpr`/accessor một dòng/deleter/type alias/macro log (`common/*.h`, `src/lotus-config.h`, `src/lotus-mode.h`, `src/lotus-log.h`, `src/emoji-entry.h`, `lotus/types.h`, `lotus/transformation.h`, `lotus/rule.h`, `test/*.h`).
- Header chỉ khai báo + doxygen; thân hàm để ở `.cpp`, trừ accessor một dòng, deleter, template, `inline constexpr`.
- Code dùng chung giữa addon và server đặt ở `common/` với tên `common-xxx.{h,cpp}`.

## 7. Đặt tên

- Type: `PascalCase`; hàm/biến: `camelCase`; thành viên: `camelCase_`; hằng/`static constexpr`: `kPascalCase` (kể cả trong class: `kMaxEntries`, `kMaxBufferedKeys`); global: `g_camelCase`; macro và hằng môi trường: `UPPER_SNAKE`.
- Enum class: `PascalCase` cho cả type lẫn member (`LotusMode::Smooth`, `KbOp::Backspace`, `LogLevel::Debug`).
- File: `kebab-case.{h,cpp}`; include guard: `_<THƯMỤC>_<TÊN_FILE>_H_` (ví dụ `_COMMON_FD_GUARD_H_`).
- Ngoại lệ: tên hàm trong `libinput_interface` (`open_restricted`, `close_restricted`) giữ đúng như tài liệu libinput.

## 8. Log & lỗi

- Addon: `LOTUS_INFO`/`LOTUS_WARN`/`LOTUS_ERROR`; server: `LotusLogger::instance().info/warn/error`.
- Thông điệp tiếng Anh, nêu hành động + đối tượng; chọn đúng mức; không log lặp trong vòng lặp nóng (log lần đầu như `UinputDevice::reportWriteFailure`).

## 9. Comment & tài liệu

- Comment trong code bằng tiếng Anh (theo codebase hiện tại); mỗi file có khối `@file`.
- Mọi khai báo công khai có doxygen (`@brief`, `@param`, `@return`).
- Không để code bị comment-out, log debug tạm, `TODO/FIXME` mồ côi, comment đùa — xoá hẳn trước khi mở PR.

## 10. Test

- Hành vi mới hoặc bug fix phải có test trong `test/`, đăng ký bằng helper CMake (`add_lotus_headless_test` / `add_lotus_unit_test`).
- Mẫu chung: `check(name, condition)` / `reportFailure(step, expected, actual, meaning)`, in `FAIL`, trả exit code khác 0 khi sai.
- Test phải deterministic, không phụ thuộc mạng/màn hình; test cần bind socket thì chạy trong namespace riêng (`LOTUS_SOCKET_NAMESPACE`).

## 11. Phạm vi PR (một PR một vấn đề)

- **Mỗi PR chỉ giải quyết đúng một vấn đề.** Cần sửa nhiều thứ ⇒ tách thành nhiều nhánh/nhiều PR.
- Không trộn refactor, format, đổi tên, cập nhật tài liệu không liên quan vào PR sửa lỗi/tính năng.
- Phát hiện việc ngoài phạm vi ⇒ ghi issue hoặc mở PR riêng, không nhét vào PR đang làm.
- PR phải: nhánh đích `dev`, build 0 warning, `ctest` xanh, commit message 1 dòng theo `type(scope): mô tả ngắn`.
