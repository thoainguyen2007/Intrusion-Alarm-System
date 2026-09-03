# Nghiên cứu module MicroSD SPI cho STM32F103 và ESP32-S3

## 1. Phạm vi tài liệu

Tài liệu này mô tả module MicroSD SPI 6 chân dạng Catalex được dùng trong dự án, gồm:

- Pinout và hướng truyền dữ liệu SPI.
- Mạch nguồn AMS1117-3.3 và buffer 74LVC125/74ABT125.
- Trình tự khởi tạo thẻ SD ở SPI mode.
- Kết nối với STM32F103C8T6 và ESP32-S3.
- Kết quả đo, log và thử nghiệm thực tế trên phần cứng.
- Các lỗi đã gặp, nguyên nhân và quy trình chẩn đoán.

Module thực tế có thứ tự chân, đọc từ phía chân `CS` sang phía `GND`:

```text
CS - SCK - MOSI - MISO - VCC - GND
```

> Luôn đọc chữ in trực tiếp trên PCB. Không suy luận thứ tự chân từ ảnh trên mạng vì ảnh có thể bị xoay hoặc lật.

---

## 2. Pinout và hướng tín hiệu

| Chân module | Hướng so với MCU | Chức năng |
|---|---|---|
| `CS` | MCU → module | Chọn thẻ, active-low |
| `SCK` | MCU → module | Clock SPI |
| `MOSI` | MCU → module | Master Out, Slave In; dữ liệu/lệnh gửi vào thẻ |
| `MISO` | Module → MCU | Master In, Slave Out; phản hồi và dữ liệu từ thẻ |
| `VCC` | Nguồn → module | Đầu vào bộ ổn áp AMS1117-3.3 |
| `GND` | Chung | Mass nguồn và tham chiếu logic |

Không được đảo `MOSI` và `MISO`. Tên chân được nhìn từ phía bộ điều khiển SPI master:

```text
MCU MOSI ───> module MOSI ───> thẻ DI/CMD
MCU MISO <─── module MISO <─── thẻ DO/DAT0
```

---

## 3. Cấu trúc điện của module

### 3.1. AMS1117-3.3

Thẻ MicroSD hoạt động ở miền điện áp khoảng 3.3 V. Module dùng AMS1117-3.3 để hạ điện áp đầu vào xuống 3.3 V.

AMS1117 không phải bộ ổn áp có dropout rất thấp theo tiêu chuẩn hiện đại. Dropout điển hình khoảng 1.0–1.2 V tùy dòng tải. Vì vậy:

```text
VCC module khuyến nghị: 5 V
AMS1117 OUT: khoảng 3.3 V
```

Nếu cấp 3.3 V vào chân `VCC`, điện áp sau AMS1117 có thể chỉ còn khoảng 2.0–2.5 V. Thẻ có thể không khởi động, MISO kẹt LOW/HIGH hoặc chỉ đọc được không ổn định.

Các điểm cần đo:

| Điểm đo | Giá trị mong đợi |
|---|---:|
| Header `VCC` so với `GND` | 4.8–5.1 V |
| Tab/chân OUT AMS1117 so với `GND` | 3.2–3.4 V |

Không mặc định chân có nhãn `5VIN` trên một board phát triển là đầu ra 5 V. Trong thử nghiệm, chân `5VIN` của ESP32-S3 chỉ đo được khoảng 1.1–1.2 V. Đây là đầu vào hoặc bị cách ly khỏi USB 5 V, không thể cấp nguồn cho module.

Giải pháp an toàn:

- Dùng chân `VBUS`, `5V_OUT` hoặc `VU` đã đo được gần 5 V; hoặc
- Dùng nguồn 5 V ngoài cho riêng module và nối chung GND với MCU.

Không nối 5 V vào chân `3V3` của ESP32/STM32.

### 3.2. Buffer 74LVC125/74ABT125

Module thường có IC quad-buffer 74LVC125 hoặc 74ABT125. Ba kênh dùng cho `CS`, `SCK`, `MOSI`; kênh còn lại đệm đường `MISO`.

Mỗi kênh có chân Output Enable active-low:

| `nOE` | Input | Output |
|---|---|---|
| LOW | LOW | LOW |
| LOW | HIGH | HIGH |
| HIGH | bất kỳ | High impedance |

Trên nhiều phiên bản Catalex, `nOE` của kênh MISO bị nối cố định xuống GND thay vì điều khiển bằng CS. Vì vậy MISO luôn được drive và không nhả bus khi CS ở HIGH. Hệ quả:

- MISO có thể vẫn LOW khi không cắm thẻ.
- Module không thích hợp để chia sẻ cùng bus SPI với thiết bị khác nếu chưa sửa mạch OE.
- MISO LOW khi tháo thẻ không tự động chứng minh IC buffer bị hỏng.

Trong dự án hiện tại, OLED dùng I2C và MicroSD là thiết bị duy nhất trên SPI1 nên lỗi không nhả MISO chưa gây tranh chấp bus.

---

## 4. Kết nối phần cứng

### 4.1. STM32F103C8T6

SPI1 mặc định của STM32F103:

| Module MicroSD | STM32F103 | Chức năng |
|---|---|---|
| `CS` | `PA4` | GPIO output, active-low |
| `SCK` | `PA5` | SPI1_SCK |
| `MOSI` | `PA7` | SPI1_MOSI |
| `MISO` | `PA6` | SPI1_MISO |
| `VCC` | Nguồn 5 V phù hợp | Không dùng đường 3.3 V qua AMS1117 |
| `GND` | GND chung | Bắt buộc chung mass |

Theo RM0008, SPI1 không remap dùng `PA5/PA6/PA7` lần lượt cho `SCK/MISO/MOSI`. Không thể đảo PA6 và PA7 khi dùng peripheral SPI1 phần cứng.

### 4.2. ESP32-S3 dùng để đối chứng

Board đối chứng được nhận diện bằng esptool:

```text
Chip: ESP32-S3 revision 0.2
Flash: 16 MB QIO
PSRAM: 8 MB OPI
USB-UART: CH343, COM26
```

Pin SPI của Arduino variant `esp32s3`:

| Module MicroSD | ESP32-S3 |
|---|---:|
| `CS` | GPIO10 |
| `MOSI` | GPIO11 |
| `SCK` | GPIO12 |
| `MISO` | GPIO13 |
| `VCC` | 5 V thật hoặc nguồn 5 V ngoài |
| `GND` | GND chung |

---

## 5. SPI mode và trình tự khởi tạo

Thiết lập ban đầu:

```text
Mode: SPI Mode 0
CPOL: 0
CPHA: cạnh thứ nhất
Data: 8 bit, MSB first
Clock khởi tạo: <= 400 kHz
CS: HIGH khi phát clock power-up
```

Trình tự khởi tạo dùng trong driver:

1. Chờ nguồn và regulator ổn định.
2. Giữ CS HIGH, MOSI HIGH và phát ít nhất 74 clock; firmware dùng 160 clock.
3. Kéo CS LOW và gửi CMD0 với CRC `0x95`.
4. Chờ R1 `0x01`, xác nhận thẻ vào SPI idle state.
5. Gửi CMD8 với argument `0x000001AA`, CRC `0x87`.
6. Lặp CMD55 + ACMD41; đặt HCS cho thẻ v2.
7. Chờ ACMD41 trả R1 `0x00`.
8. Gửi CMD58 và đọc OCR.
9. Kiểm tra CCS trong OCR để phân biệt SDSC với SDHC/SDXC.
10. Chỉ tăng clock sau khi thẻ sẵn sàng.

Các lệnh đọc/ghi sector:

| Lệnh | Chức năng |
|---|---|
| CMD9 | Đọc CSD |
| CMD17 | Đọc một block 512 byte |
| CMD24 | Ghi một block 512 byte |
| CMD13 | Đọc trạng thái thẻ |

SDHC/SDXC dùng địa chỉ block trực tiếp. SDSC dùng địa chỉ byte, tức `sector × 512`.

---

## 6. Ý nghĩa phản hồi và lỗi quan sát được

### 6.1. R1 thường gặp

| R1 | Ý nghĩa |
|---:|---|
| `0xFF` | Không nhận phản hồi; MISO HIGH/hở hoặc module chưa có nguồn |
| `0x01` | Thẻ đang idle, CMD0 thành công |
| `0x00` | Thẻ sẵn sàng |
| Bit `0x04` | Illegal command |
| Bit `0x08` | CRC error |

### 6.2. Các giai đoạn thử nghiệm thực tế

#### Giai đoạn nguồn sai

STM32 nhận hơn 3000 byte đều là `0xFF`:

```text
[SD] Init: No SD response (R1=0xFF)
[SD] SPI RX bytes: FF=3044, 00=0, other=0
```

ESP32-S3 từng nhận MISO LOW liên tục:

```text
sdWait(): Wait Failed
GO_IDLE_STATE failed
sdSelectCard(): Select Failed
```

Nguyên nhân quan trọng được phát hiện: chân `5VIN` dùng cấp module chỉ có 1.1–1.2 V.

#### Giai đoạn SPI vật lý hoạt động

Sau khi cấp nguồn phù hợp, firmware raw chỉ-đọc nhận được:

```text
CMD0     R1=0x01 (idle)
CMD8     R1=0x01 (idle)
ACMD41   R1=0x00 (ready)
CMD58    R1=0x00 (ready)
OCR      C0 FF 80 00
CMD9     R1=0x00 (ready)
CMD17    R1=0x00 (ready)
```

Điều này chứng minh module, bốn đường SPI và card controller đều hoạt động.

#### Giai đoạn filesystem không hợp lệ

Sector 0 từng đọc được toàn `0xFF`, boot signature là `FF FF` thay vì `55 AA`. FatFs báo:

```text
There is no valid FAT volume
```

Đây là lỗi filesystem/partition, không còn là lỗi CMD0 hay wiring.

#### Giai đoạn format và ghi/đọc thành công

Sau khi tạo lại FAT và giữ SPI ở tốc độ ổn định 400 kHz:

```text
RESULT: SD init OK, type=SDHC/SDXC
Card size: 15000 MB
Total: 14983 MB, used: 0 MB
WRITE: OK
READ-BACK: ESP32-S3 SD test, uptime=2344 ms
ESP32-S3 SD test, uptime=2356 ms
READ-BACK: OK
```

Kết luận thực nghiệm cuối:

- Thẻ là SDHC/SDXC dung lượng danh nghĩa 16 GB.
- Module đọc và ghi được.
- FAT mount thành công.
- File `/esp32s3_test.txt` được tạo và đọc lại chính xác.
- 400 kHz là cấu hình kiểm thử ổn định với bộ dây hiện tại.

Các kết quả CSD/dung lượng đọc khi nguồn chưa ổn định không được dùng làm giá trị chính thức; kết quả cuối sau khi nguồn đúng và FAT mount thành công là khoảng 15 GB khả dụng.

---

## 7. Quy trình chẩn đoán khuyến nghị

Thực hiện theo thứ tự, không bắt đầu bằng việc sửa filesystem:

1. Đo VCC module gần 5 V.
2. Đo output AMS1117 gần 3.3 V.
3. Xác nhận chung GND.
4. Xác nhận đúng thứ tự `CS-SCK-MOSI-MISO-VCC-GND` trên PCB.
5. Khởi tạo ở 100–400 kHz, SPI Mode 0.
6. Kiểm tra CMD0 phải trả `0x01`.
7. Kiểm tra CMD8/ACMD41/CMD58.
8. Đọc CMD9 và CMD17 sector 0.
9. Chỉ kiểm tra FAT sau khi CMD17 thành công.
10. Chỉ thử ghi sau khi nguồn ổn định; ghi tạo xung dòng lớn hơn đọc.

Phân loại nhanh:

| Hiện tượng | Tầng lỗi ưu tiên kiểm tra |
|---|---|
| Toàn `0xFF` | Nguồn, MISO hở, sai dây, CS/SCK không chạy |
| Toàn `0x00` | Thiếu nguồn, MISO bị buffer kéo thấp, card bận/kẹt |
| CMD0 OK nhưng mount lỗi | Partition/filesystem |
| Đọc OK, ghi lỗi | Nguồn sụt khi ghi, thẻ read-only/hỏng, tín hiệu |
| 400 kHz OK, clock cao lỗi | Dây dài, breadboard, buffer/module chất lượng thấp |

---

## 8. Khuyến nghị cho firmware STM32

- Giữ clock khởi tạo `SPI_BAUDRATEPRESCALER_256` (~281.25 kHz ở PCLK2 72 MHz).
- Sau init chỉ tăng clock khi đã xác nhận phần cứng ổn định; có thể bắt đầu bằng prescaler 64 hoặc 32 thay vì 8.
- Luôn log R1 của từng giai đoạn.
- Có timeout cho wait-ready, data token và write-busy.
- Gọi sync trước khi ngắt nguồn.
- Không format tự động trong firmware sản phẩm; chỉ format khi người dùng xác nhận.
- Với module Catalex, không chia sẻ MISO cùng thiết bị SPI khác nếu chưa sửa OE.
- Ưu tiên dây ngắn, GND chắc và tụ decoupling gần module.

---

## 9. Tài liệu tham khảo

1. STMicroelectronics, **RM0008 – STM32F101/102/103/105/107 Reference Manual**:
   https://www.st.com/resource/en/reference_manual/rm0008-stm32f103xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
2. SD Association, **Physical Layer Simplified Specification**:
   https://www.sdcard.org/downloads/pls/
3. Nexperia, **74LVC125A Quad Buffer/Line Driver Datasheet**:
   https://assets.nexperia.com/documents/data-sheet/74LVC125A.pdf
4. Catalex MicroSD module schematic:
   https://nettigo.eu/attachments/531
5. AMS1117-3.3 specifications:
   https://www.datasheets.com/advanced-monolithic-systems/AMS1117-3.3
6. Espressif Arduino ESP32 core, SD SPI implementation (`sd_diskio.cpp`), phiên bản thử nghiệm 3.3.11:
   https://github.com/espressif/arduino-esp32

---

## 10. Trạng thái xác nhận

Cập nhật 03/09/2026: MicroSD đã tích hợp trên STM32 với firmware 2.3. Người vận hành xác nhận sự cố `CMD0 failed` vừa gặp do gắn chưa chắc; sau khi gắn lại, hệ thống hoạt động bình thường mà không sửa driver. Các chẩn đoán nguồn/tiếp xúc ở trên được giữ như lịch sử nghiên cứu, không phải lỗi đang còn mở của mô hình hiện tại.

| Hạng mục | Trạng thái |
|---|---|
| Pinout module | Đã xác nhận |
| Nguồn 5 V đầu vào | Đã xác định là yêu cầu quan trọng |
| Output AMS1117 3.3 V | Cần luôn đo khi lắp sang board khác |
| SPI CMD0/CMD8/ACMD41/CMD58 | Đã chạy thành công trên ESP32-S3 |
| Đọc sector | Đã chạy thành công |
| FAT filesystem | Đã tạo và mount thành công |
| Ghi file | Đã chạy thành công |
| Đọc lại file | Đã chạy thành công |
| Tích hợp FatFs STM32 | Đã tích hợp trên main: init, đọc sector 0, mount và ghi/sync LOG.TXT thành công |

Log vận hành STM32 do người dùng cung cấp:

```text
[SD] Init: OK (R1=0x00)
[SD] Type: SDHC/SDXC, OCR=0xC0FF8000
[SD] Read sector 0: OK
[SD] First bytes: 00 00 00 00, signature: 55 AA
[FATFS] Mount: OK (FR=0)
[FATFS] Capacity: 15343600 KiB, free: 15343432 KiB
[FATFS] Physical sync LOG.TXT: OK (seq=5, 83 bytes), queued=0
```

Năm bản ghi khởi động đã được ghi/sync, hàng đợi hết dữ liệu. Đây là xác nhận vận hành, không phải kiểm thử mất nguồn giữa lúc ghi. Firmware hiện khởi tạo khoảng 281.25 kHz rồi dùng khoảng 9 MHz; kết quả thử 400 kHz trên ESP32-S3 trong mục 6 là lịch sử đối chứng, không phải tốc độ runtime STM32.
