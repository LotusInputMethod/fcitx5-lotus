# Mô tả

<!-- Mô tả rõ ràng và chi tiết thay đổi của bạn: vấn đề gì, giải quyết thế nào. -->

## Loại thay đổi

- [ ] Tính năng mới
- [ ] Sửa lỗi
- [ ] Cải thiện hiệu năng
- [ ] Cập nhật tài liệu
- [ ] Cải tiến CI/CD

## Liên kết issue

<!-- Fixes #123 (nếu có) -->

## Screenshot (cho UI changes)

<!-- Dán screenshot trước/sau nếu thay đổi giao diện -->

# Checklist

- [ ] Đã đọc file `CONTRIBUTING.md` của dự án
- [ ] Nhánh đích là `dev` (KHÔNG phải `main`)
- [ ] PR này chỉ giải quyết **một** vấn đề duy nhất (nhiều vấn đề ⇒ tách nhiều PR)
- [ ] Không trộn refactor/format/tài liệu ngoài phạm vi
- [ ] Đã chạy `clang-format` (tuân thủ `.clang-format`)
- [ ] Build C++ thành công (`cmake .. && make`)
- [ ] Test pass (`cmake --build build && ctest --test-dir build --output-on-failure`)
- [ ] Đã rebase với nhánh `dev` mới nhất
- [ ] Các CI checks pass:
