[English](CONTRIBUTING.en.md) | [Tiếng Việt](CONTRIBUTING.md)

# Đóng góp cho fcitx5-lotus

Cảm ơn bạn quan tâm đến việc đóng góp cho dự án fcitx5-lotus! Tài liệu này hướng dẫn bạn cách tham gia phát triển dự án.

## 📋 Mục lục

- [Bắt đầu](#bắt-đầu)
- [Quy trình đóng góp](#quy-trình-đóng-góp)
- [Quy tắc ứng xử](#quy-tắc-ứng-xử)
- [Quy tắc code style](#quy-tắc-code-style)
- [Quy trình Pull Request](#quy-trình-pull-request)
- [Lưu ý quan trọng về nhánh](#lưu-ý-quan-trọng-về-nhánh)
- [Báo cáo lỗi](#báo-cáo-lỗi)
- [Đề xuất tính năng](#đề-xuất-tính-năng)

## Bắt đầu

### Yêu cầu hệ thống

- GCC hoặc Clang với hỗ trợ C++17
- CMake >= 3.16
- Go 1.20+ (cho bamboo engine)
- Fcitx5 development headers
- Git

### Cài đặt và build

```bash
# Clone repository
git clone https://github.com/LotusInputMethod/fcitx5-lotus.git
cd fcitx5-lotus

# Khởi tạo submodules
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Quy trình đóng góp

### 1. Fork repository

Fork repository này trên GitHub và clone fork của bạn về máy.

```bash
git clone https://github.com/yourusername/fcitx5-lotus.git
cd fcitx5-lotus
git remote add upstream https://github.com/LotusInputMethod/fcitx5-lotus.git
```

### 2. Tạo nhánh mới

Luôn tạo nhánh mới cho mỗi tính năng hoặc sửa lỗi:

```bash
git checkout dev
git pull upstream dev
git checkout -b feature/ten-tinh-nang
# hoặc
git checkout -b fix/ten-loi
```

### 3. Thực hiện thay đổi

- Viết code sạch, dễ đọc
- Tuân thủ quy tắc code style (xem bên dưới)
- Cập nhật tài liệu nếu cần

### 4. Commit changes

Sử dụng commit messages rõ ràng và mô tả:

```
feat: add emoji support

- Add emoji_data.h
- Update generate_emoji_header.py
```

## Quy tắc ứng xử

Mọi thành viên tham gia đóng góp cho dự án này đều phải tuân thủ [Quy tắc Ứng xử cho Cộng tác viên](CODE_OF_CONDUCT.md) để xây dựng một cộng đồng cởi mở, thân thiện và lành mạnh.

## Quy tắc code style

- Tuân thủ file [`.clang-format`](.clang-format) trong repository
- Khuyến khích chạy lệnh `clang-format` để định dạng code trước khi commit, hoặc sử dụng các plugin clang-format của các IDE
- Nếu muốn nhanh thì nên thêm .git/hooks/pre-commit

```sh
#!/bin/bash
shopt -s globstar nullglob
clang-format -i src/**/*.{cpp,h}
clang-format -i server/**/*.{cpp,h}
```

## Quy trình Pull Request

### 1. Đảm bảo code sạch bằng `clang-format`

### 2. Chạy test

```bash
# Build C++ code
cd build
cmake .. && make
```

### 3. Rebase với nhánh dev

```bash
git checkout dev
git pull upstream dev
git checkout feature/ten-tinh-nang
git rebase dev
```

### 4. Push và tạo PR

```bash
git push origin feature/ten-tinh-nang
```

Tạo Pull Request trên GitHub với:

- Tiêu đề mô tả rõ ràng
- Mô tả chi tiết thay đổi
- Liên kết đến issue liên quan (nếu có)
- Screenshot cho UI changes

## Lưu ý quan trọng về nhánh

### QUAN TRỌNG: TẤT CẢ PR MERGE VÀO NHÁNH DEV

**KHÔNG BAO GIỜ tạo Pull Request vào nhánh `main`**

- Nhánh `main` chỉ chứa bản release ổn định và chỉ được merge bởi maintainer
- Tất cả Pull Request phải nhắm vào nhánh `dev`
- Sau khi pass tất cả các bài CI/CD test và được maintainer review, code sẽ được merge vào `dev`
- Khi đủ điều kiện, code sẽ được merge từ `dev` sang `main` bởi maintainer để bump version

### Cấu trúc nhánh

```
main    ← Bản release ổn định (chỉ maintainer được merge)
  ↑
dev     ← Nhánh phát triển chính (tất cả PR merge vào đây)
  ↑
feature/*, fix/*, hotfix/*  ← Nhánh cá nhân cho mỗi PR
```

### Quy trình merge

1. Developer tạo PR vào `dev`
2. Code review bởi maintainer
3. Merge vào `dev`
4. Test trên `dev`
5. Khi ổn định → merge `dev` → `main` (bởi maintainer)

## Báo cáo lỗi

Khi báo cáo lỗi, vui lòng cung cấp:

- Phiên bản fcitx5-lotus
- Hệ điều hành và phiên bản
- Các bước để tái hiện lỗi
- Log hoặc screenshot (nếu có) (Log bằng lệnh fcitx5-diagnose)
- Môi trường (desktop environment, terminal, v.v.)

## Đề xuất tính năng

Trước khi đề xuất tính năng mới:

- Kiểm tra xem tính năng đã tồn tại chưa
- Tìm kiếm issue tương tự
- Mô tả rõ ràng tính năng và use case
- Giải thích tại sao tính năng này cần thiết

## Giấy phép

Bằng cách đóng góp, bạn đồng ý rằng code của bạn sẽ được cấp phép theo cùng giấy phép với dự án (GPL-3.0-or-later).

---

Cảm ơn bạn đã đóng góp!
