# Golden corpus (nguồn sự thật 1-1 cho bản port C++)

Ba file ở đây do chính engine Go gốc sinh ra, dùng để kiểm chứng bản port C++ khớp từng byte.
Engine Go đó là **[bamboo-core](https://github.com/BambooEngine/bamboo-core)** (MIT, © 2018 Luong Thanh
Lam); bản port C++ nằm trong `lotus/`, phát hành dưới GPL-3.0-or-later, và bản MIT được giữ tại
[`../../lotus/LICENSE.bamboo-core`](../../lotus/LICENSE.bamboo-core) — xem thêm [`../../CREDITS`](../../CREDITS).

| File | Nội dung | Cột |
|---|---|---|
| `charset.tsv` | mọi bảng mã, mỗi rune trong danh sách vàng | `charset`, `codepoint`, `hexEncodedBytes` |
| `engine_core.tsv` | hành vi `bamboo-core` (`ProcessKey`/`GetProcessedString`/`IsValid`) | `case`, `step`, `keyHex`, `mode`, `flags`, `w2u`, `bracket`, `processedHex`, `isValid` |
| `key_processor.tsv` | hành vi wrapper (`FcitxBambooEngine.preeditProcessKeyEvent`) | `case`, `step`, `keyHex`, `state`, `opts`, `flags`, `w2u`, `bracket`, `accepted`, `preeditHex`, `commitHex` |

- Chuỗi UTF-8 được hex-hoá để file chỉ chứa ASCII.
- `flags`/`w2u`/`bracket`/`opts` ở mỗi dòng là giá trị của cả case: test C++ khởi tạo engine ở dòng đầu
  tiên của mỗi `case` rồi phát lại tuần tự.
- `mode` (engine core) là mode truyền cho `ProcessKey`: `VietnameseMode`, `EnglishMode`,
  `VietnameseMode|ToneLess`, `VietnameseMode|InReverseOrder`, `VietnameseMode|PunctuationMode`, và `0`
  cho bước `FcitxBackSpace`.
- `state` (wrapper) là mask modifier: `0`, `Lock`, `Shift`, `Shift|Lock` (những mask `isValidState`
  chấp nhận). Cột này **tách riêng** khỏi `mode`, không dùng chung một trường như bản corpus đầu.
- `opts` là bitmask: 1 = autoNonVnRestore, 2 = ddFreeStyle, 4 = spellCheckWithDicts, 8 = macroEnabled
  (macro tĩnh `vn`/`ko`). Bit 4 **được bật ngẫu nhiên** nhưng vẫn deterministic: dumper nạp danh sách
  3 từ cố định (`vietnam`, `nguoi`, `duong`) vào `newTestEngine`, và test C++ nạp đúng 3 từ đó bằng
  `Dictionary::loadFromWords` ⇒ không bao giờ đọc file từ điển bên ngoài.
- Corpus = 68 chuỗi curated + 2000 chuỗi ngẫu nhiên (seed `20261007`), mỗi chuỗi dài 1..12 phím, kèm
  biến thể có chèn `FcitxBackSpace` ở giữa. Chuỗi curated nhắm vào nhánh hiếm: bracket transform sát
  ranh giới từ, đuôi shortcut `uwo`, macro `vn `/`ko.`, `Tab`, hai bracket-key liên tiếp.

## Tái sinh

Corpus **đã commit là nguồn sự thật** và **không tái sinh được trong repo này nữa**: engine Go và dumper
(`golden_dump_test.go`) đã được chuyển sang repo tham chiếu riêng
[**LotusInputMethod/bamboo**](https://github.com/LotusInputMethod/bamboo) (chỉ để đối chiếu logic, không
khuyến khích sử dụng). Muốn sinh lại corpus thì chạy dumper ở repo đó — hướng dẫn nằm trong README của
nó; corpus mới **bắt buộc** phải chạy lại toàn bộ test C++, và sửa golden bằng tay là mất tính 1-1.

## Quét locally (không commit)

Phía C++, `LOTUS_GOLDEN_DIR` đổi thư mục golden lúc chạy (mặc định vẫn là `LOTUS_TEST_GOLDEN_DIR` được
biên dịch vào), nên có thể trỏ replay sang một corpus lớn hơn có sẵn (ví dụ bản dump do cây Go lịch sử
sinh ra):

```sh
LOTUS_GOLDEN_DIR=/tmp/golden-big ./build/test/lotus_engine_test
LOTUS_GOLDEN_DIR=/tmp/golden-big ./build/test/lotus_key_processor
```
