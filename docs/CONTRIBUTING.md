[English](CONTRIBUTING.en.md) | [Tiếng Việt](CONTRIBUTING.md)

# Đóng góp cho fcitx5-lotus

Cảm ơn bạn quan tâm đến việc đóng góp cho dự án fcitx5-lotus! Tài liệu này hướng dẫn bạn cách tham gia phát triển dự án.

## 📋 Mục lục

- [📖 Tài liệu kỹ thuật chuyên sâu](#tài-liệu-kỹ-thuật-chuyên-sâu)
- [⚙️ Bắt đầu](#bắt-đầu)
  - [Yêu cầu hệ thống](#yêu-cầu-hệ-thống)
  - [Cài đặt và build](#cài-đặt-và-build)
  - [Kiểm tra thay đổi (Testing)](#kiểm-tra-thay-đổi-testing)
- [🤝 Quy trình đóng góp](#quy-trình-đóng-góp)
  - [1. Fork và tạo nhánh](#1-fork-và-tạo-nhánh)
  - [2. Thực hiện thay đổi & Commit](#2-thực-hiện-thay-đổi--commit)
  - [3. Quy trình Pull Request](#3-quy-trình-pull-request)
  - [Lưu ý quan trọng về nhánh](#lưu-ý-quan-trọng-về-nhánh)
- [📝 Quy tắc & Tiêu chuẩn](#quy-tắc--tiêu-chuẩn)
- [🐛 Báo cáo lỗi & Đề xuất tính năng](#báo-cáo-lỗi--đề-xuất-tính-năng)
- [⚖️ Giấy phép](#giấy-phép)

---

## 📖 Tài liệu kỹ thuật chuyên sâu

Trước khi bắt đầu thực hiện thay đổi, vui lòng tham khảo các tài liệu sau để nắm vững kiến trúc của Lotus:

- **[Kiến trúc hệ thống](ARCHITECTURE.md)**: Giải thích về sự kết hợp C++/Go, IPC và server-client.
- **[Các chế độ gõ chuyên sâu](input_modes.md)**: Chi tiết kỹ thuật về Smooth, Slow, Surrounding, Preedit...
- **[Tra cứu thiết lập](settings_reference.md)**: Danh sách và ý nghĩa các tham số cấu hình trong engine.

---

## ⚙️ Bắt đầu

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

# Khởi tạo submodules (quan trọng cho bamboo core)
git submodule update --init --recursive

# Build toàn bộ dự án
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=/usr/lib ..
make -j$(nproc)
sudo make install
```

#### Build từng thành phần (Dành cho Debug)

Nếu bạn chỉ chỉnh sửa UI cấu hình:

- Chạy trực tiếp `python3 settings-gui/main.py` (yêu cầu `PySide6`).

Nếu bạn chỉnh sửa Bamboo core (Go):

- Chạy `go build` trong thư mục `bamboo/`.

### Kiểm tra thay đổi (Testing)

1. **Restart Fcitx5**: Sau khi `make install`, hãy chạy `fcitx5 -r` để áp dụng thay đổi.
2. **Kiểm tra Log**: Sử dụng `journalctl -f` hoặc kiểm tra log của Fcitx5 để xem các thông báo DEBUG từ Lotus.
3. **Uinput Server**: Đảm bảo `fcitx5-lotus-server` đang chạy nếu bạn test chế độ Smooth/Slow.

---

## 🤝 Quy trình đóng góp

### 1. Fork và tạo nhánh

1. **Fork** repository này trên GitHub và clone về máy:

    ```bash
    git clone https://github.com/yourusername/fcitx5-lotus.git
    cd fcitx5-lotus
    git remote add upstream https://github.com/LotusInputMethod/fcitx5-lotus.git
    ```

2. **Tạo nhánh mới** từ nhánh `dev`:

    ```bash
    git checkout dev
    git pull upstream dev
    git checkout -b feature/ten-tinh-nang # hoặc fix/ten-loi
    ```

### 2. Thực hiện thay đổi & Commit

- Viết code sạch, dễ đọc và tuân thủ quy tắc code style.
- Cập nhật tài liệu tương ứng nếu có thay đổi về tính năng hoặc cấu hình.
- Sử dụng commit messages rõ ràng (`feat:`, `fix:`, `docs:`, ...):

    ```bash
    git commit -m "feat: add emoji support"
    ```

### 3. Quy trình Pull Request

1. **Đảm bảo code sạch**: Sử dụng `clang-format` và kiểm tra kỹ logic.
2. **Chạy test**: Build lại code và kiểm tra tính ổn định.

    ```bash
    cd build && cmake .. && make
    ```

3. **Rebase với nhánh dev**:

    ```bash
    git checkout dev && git pull upstream dev
    git checkout feature/ten-tinh-nang
    git rebase dev
    ```

4. **Push và tạo PR**:

    ```bash
    git push origin feature/ten-tinh-nang
    ```

    Tạo PR trên GitHub trỏ vào nhánh **`dev`** của upstream.

### Lưu ý quan trọng về nhánh

#### QUAN TRỌNG: TẤT CẢ PR MERGE VÀO NHÁNH DEV

**KHÔNG BAO GIỜ tạo Pull Request vào nhánh `main`**

- Nhánh `main` chỉ chứa bản release ổn định.
- Tất cả PR phải nhắm vào nhánh `dev`.
- Sau khi pass tất cả các bài CI/CD test và được maintainer review, code sẽ được merge vào `dev`.
- Khi đủ điều kiện, code sẽ được merge từ `dev` sang `main` bởi maintainer để bump version.

#### Cấu trúc nhánh

```text
main    ← Bản release ổn định (chỉ maintainer được merge)
  ↑
dev     ← Nhánh phát triển chính (tất cả PR merge vào đây)
  ↑
feature/*, fix/*, hotfix/*  ← Nhánh cá nhân cho mỗi PR
```

#### Quy trình merge

1. Developer tạo PR vào `dev`.
2. Code review bởi maintainer.
3. Merge vào `dev`.
4. Test trên `dev`.
5. Khi ổn định → merge `dev` → `main` (bởi maintainer).

---

## 📝 Quy tắc & Tiêu chuẩn

### Quy tắc ứng xử

Mọi đóng góp phải tuân thủ [Quy tắc Ứng xử](CODE_OF_CONDUCT.md) để xây dựng cộng đồng lành mạnh.

### Quy tắc code style

- Tuân thủ file [`.clang-format`](../.clang-format).
- Khuyến khích tạo một hook cho pre-commit để tự động format code trước khi commit bằng cách tạo file `.git/hooks/pre-commit` với nội dung:

```bash
#!/bin/bash
FILES=$(git diff --cached --name-only --diff-filter=ACMR | grep -E '\.(cpp|h)$')

if [ -n "$FILES" ]; then
    for file in $FILES; do
        clang-format -i "$file"
        git add "$file"
    done
fi
```

Sau đó chạy lệnh: `chmod +x .git/hooks/pre-commit`

---

## 🐛 Báo cáo lỗi & Đề xuất tính năng

### Báo cáo lỗi

Vui lòng cung cấp: Phiên bản, HĐH, Các bước tái hiện, Log (`fcitx5-diagnose`) và Screenshot.

### Đề xuất tính năng

Mô tả rõ ràng tính năng, use case và tại sao nó cần thiết. Kiểm tra xem đã có đề xuất tương tự chưa.

---

## ⚖️ Giấy phép

Bằng cách đóng góp, bạn đồng ý rằng code của bạn sẽ được cấp phép theo cùng giấy phép với dự án (**GPL-3.0-or-later**).

---

Cảm ơn bạn đã đóng góp!
