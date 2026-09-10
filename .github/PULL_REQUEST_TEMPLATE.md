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

- [ ] Nhánh đích là `dev` (KHÔNG phải `main`)
- [ ] Đã chạy `clang-format` (tuân thủ `.clang-format`)
- [ ] Đã kiểm tra `ruff check` và `ruff format` (cho phần Python `settings-gui`)
- [ ] Build C++ thành công (`cmake .. && make`)
- [ ] Test pass (`cd bamboo/ && go test -race ./...`)
- [ ] Đã rebase với nhánh `dev` mới nhất
- [ ] Các CI checks pass:
