# Tra cứu các thiết lập (Settings Reference)

Tài liệu này giải thích chi tiết các tùy chọn có trong giao diện cấu hình của Lotus.

## Các thiết lập chung (General Settings)

| Thiết lập | Ý nghĩa | Hành vi nội bộ |
| :--- | :--- | :--- |
| **Kiểu gõ** | Chọn Telex, VNI hoặc VIQR. | Chuyển đổi bộ parser trong nhân Bamboo (Go core). |
| **Chế độ gõ** | Smooth, Surrounding, Preedit, v.v. | Thay đổi cơ chế commit (Uinput vs Fcitx API). Xem [Chế độ gõ](input_modes.md). |
| **Bỏ dấu tự do** | Cho phép đặt dấu ở bất kỳ đâu trong từ. | Tắt kiểm tra tính hợp lệ của dấu trong Bamboo core. |
| **Gõ tắt** | Bật/tắt tính năng gõ tắt cá nhân. | Tra cứu bảng từ thay thế trước khi xử lý phím. |

## Thiết lập nâng cao (Advanced Settings)

| Thiết lập | Ý nghĩa | Hành vi nội bộ |
| :--- | :--- | :--- |
| **Dùng uinput server** | Bật giao tiếp với `fcitx5-lotus-server`. | Mở Unix Domain Socket để gửi lệnh phím. |
| **Tự động viết hoa** | Tự động viết hoa chữ cái đầu câu. | Phân tích Surrounding Text để tìm dấu chấm/xuống dòng. |
| **Hiện overlay phím** | Hiển thị phím vừa gõ trên màn hình. | Phát tín hiệu đến client overlay qua socket. |
| **Chế độ Minecraft** | Tối ưu hóa phím xóa cho Minecraft Java. | Thay đổi timing và phương thức lặp phím Backspace. |

## Thao tác phím (Key Bindings)

| Phím tắt | Chức năng |
| :--- | :--- |
| **Ctrl + `** | Mở menu chuyển nhanh chế độ gõ. |
| **Nalt + L** | Bật/Tắt nhanh bộ gõ. |
| **Shift** | (Tùy chọn) Chuyển giữa tiếng Việt và tiếng Anh. |

---

*Lưu ý: Một số thiết lập yêu cầu khởi động lại Fcitx5 hoặc Lotus Server để có hiệu lực.*
