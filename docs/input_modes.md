# Chi tiết các chế độ gõ (Input Modes)

- **[Tra cứu thiết lập](settings_reference.md)**: Danh sách và ý nghĩa các tham số cấu hình trong engine.

Lotus cung cấp nhiều chế độ gõ để tương thích với các ứng dụng khác nhau trên Linux. Mỗi chế độ có cách xử lý văn bản khác nhau.

## 1. Smooth Mode (Chế độ mượt) - Ưu tiên

- **Kỹ thuật**: Sử dụng `uinput` để gửi các mã phím Backspace và ký tự thực tế ở mức kernel.
- **Ưu điểm**: Hoạt động cực kỳ mượt mà, không gặp lỗi lặp chữ hoặc mất ký tự trong các ứng dụng phức tạp (như game, Electron, terminal).
- **Yêu cầu**: Cần chạy `fcitx5-lotus-server` và có quyền truy cập `/dev/uinput`.

## 2. Slow Mode (Chế độ chậm)

- **Kỹ thuật**: Tương tự Smooth Mode nhưng có độ trễ nhỏ giữa các lệnh Backspace và Commit.
- **Sử dụng**: Dành cho các ứng dụng cực kỳ cũ hoặc máy cấu hình thấp nơi Smooth Mode vẫn quá nhanh khiến ứng dụng không kịp xử lý phím xóa.

拍## 3. Surrounding Mode (Chế độ bao quanh)

- **Kỹ thuật**: Sử dụng chuẩn `GetSurroundingText` của Fcitx5. Engine "hỏi" ứng dụng văn bản xung quanh con trỏ để quyết định xóa bao nhiêu ký tự.
- **Ưu điểm**: Không cần quyền root/uinput.
- **Hạn chế**: Chỉ hoạt động tốt trên các ứng dụng hỗ trợ tốt chuẩn Surrounding Text (như Chrome, GTK/Qt hiện đại). Không hoạt động trong game hoặc một số terminal.

## 4. Preedit Mode (Chế độ gõ nháp)

- **Kỹ thuật**: Hiển thị chữ đang gõ trong một vùng đệm tạm thời (gạch chân) trước khi nhấn Enter hoặc Space để chuyển vào ứng dụng.
- **Sử dụng**: Khi các chế độ trên đều thất bại, hoặc người dùng muốn kiểm soát chính xác từng từ trước khi commit.

## 5. Minecraft Mode

- **Kỹ thuật**: Một biến thể của Smooth Mode được tối ưu hóa đặc biệt cho Minecraft (Java Edition) để tránh việc bị mất phím khi chat hoặc gõ lệnh trong game.

## 6. Emoji Mode

- **Kỹ thuật**: Chế độ gõ tắt hỗ trợ chèn nhanh các biểu tượng cảm xúc.

---

### Bảng so sánh nhanh

| Chế độ | Hiệu năng | Độ tương thích | Yêu cầu |
| :--- | :--- | :--- | :--- |
| **Smooth** | Rất cao | Rất rộng (Game/App) | uinput-server |
| **Surrounding** | Cao | App hiện đại (GTK/Qt) | Không |
| **Preedit** | Trung bình | Tất cả | Không |
| **Slow** | Thấp | App cũ/lỗi | uinput-server |
