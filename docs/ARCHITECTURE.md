# Kiến trúc hệ thống fcitx5-lotus

Tài liệu này mô tả chi tiết kiến trúc kỹ thuật của bộ gõ Lotus, bao gồm sự kết hợp giữa C++, Go và các cơ chế IPC (Inter-Process Communication).

## 1. Tổng quan các thành phần

Lotus được thiết kế theo mô hình lai (hybrid) để tận dụng hiệu năng của C++ cho hệ thống input method và khả năng xử lý ngôn ngữ mạnh mẽ của Go.

| Thành phần | Ngôn ngữ | Vai trò |
| :--- | :--- | :--- |
| **Addon Fcitx5** | C++ | Thành phần chính tích hợp vào framework Fcitx5, quản lý trạng thái và UI. |
| **Bamboo Core** | Go | Nhân xử lý tiếng Việt thư viện [Bamboo](https://github.com/luatnd/bamboo). |
| **CGO Wrapper** | C++/Go | Cầu nối cho phép C++ gọi các hàm xử lý từ Go. |
| **Lotus Server** | C++ | Một service riêng biệt quản lý thiết bị `/dev/uinput` để mô phỏng phím (chế độ Smooth/Slow). |

## 2. Luồng xử lý sự kiện phím (Key Event Flow)

Khi người dùng nhấn một phím, quy trình xử lý diễn ra như sau:

1. **Fcitx5 Addon**: Nhận sự kiện phím từ framework Fcitx5.
2. **Bamboo Core (via CGO)**: Addon gửi phím đến nhân Go để kiểm tra xem có phải phím điều dấu hoặc phím chức năng không.
3. **Xử lý Logic**:
    - Nếu là tiếng Việt: Nhân Go trả về chuỗi văn bản đã xử lý.
    - Nếu không: Phím được trả lại cho hệ thống xử lý bình thường.
4. **Output (Xuất kết quả)**: Tùy vào chế độ gõ:
    - **Surrounding/Preedit**: Sử dụng API chuẩn của Fcitx5 để xóa và chèn văn bản.
    - **Smooth/Slow**: Gửi lệnh qua Unix Domain Socket đến `fcitx5-lotus-server`.
5. **Lotus Server**: Nhận lệnh và ghi trực tiếp vào `/dev/uinput` để tạo ra các sự kiện Backspace và phím ký tự thực tế.

## 3. Cơ chế IPC (Inter-Process Communication)

Lotus sử dụng hai loại giao tiếp chính:

### CGO (C + Go)

Sử dụng để nhúng thư viện xử lý tiếng Việt trực tiếp vào engine bộ gõ. Điều này đảm bảo tốc độ xử lý nhanh nhất mà không có độ trễ mạng hoặc socket.

### Unix Domain Sockets

Được sử dụng giữa Addon (Client) và `fcitx5-lotus-server` (Server).

- **Socket Path**: Thường nằm tại `/tmp/fcitx5-lotus.sock` hoặc thư mục runtime của người dùng.
- **Mục đích**: Chế độ Smooth cần quyền ghi vào `/dev/uinput`. Để bảo mật, chỉ có Server chạy với quyền cao hơn (hoặc thuộc group `uinput`) chịu trách nhiệm này, trong khi Addon chạy với quyền người dùng bình thường.

## 4. Các thư mục quan trọng

- `./src`: Chứa mã nguồn C++ của engine fcitx5.
- `./bamboo`: Chứa code Go và các file export CGO (`./bamboo/bamboo-c.go`).
- `./server`: Chứa mã nguồn của uinput server.
- `./settings-gui`: Giao diện cấu hình viết bằng Python/PySide6.

---

*Tài liệu này được duy trì để hỗ trợ các nhà phát triển hiểu sâu về cách hoạt động của Lotus.*
