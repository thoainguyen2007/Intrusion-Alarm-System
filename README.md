# HỆ THỐNG BÁO ĐỘNG XÂM NHẬP (INTRUSION ALARM SYSTEM)
### STM32F103C8T6 (ARM Cortex-M3 @ 72MHz)

> **Tên đề tài tiếng Anh:** Intrusion Alarm System Based on Reed Switch & PIR with EXTI on STM32F103  
> **Tên đề tài tiếng Việt:** Hệ thống Báo Động Xâm Nhập Dựa trên Công tắc Từ & PIR với Ngắt ngoài EXTI trên STM32F103  
> **Nhóm sinh viên thực hiện (4 thành viên):**
> 1. **Nguyễn Lê Hữu Thoại** (2511006) - *Nhóm trưởng*: Thiết kế mô hình FSM, tích hợp hệ thống, xử lý I/O, viết tài liệu.
> 2. **Hàng Tuấn Bảo** (2510438): Khối cảm biến đầu vào (INPUT: Reed, PIR, SW-420, mạch chống nhiễu, EXTI).
> 3. **Nguyễn Phước Thành** (2550517): Khối thiết bị đầu ra (OUTPUT: LED, Buzzer, OLED, Nguồn, Đi dây phần cứng).
> 4. **Nguyễn Đăng Khoa** (2551799): Phần mềm cốt lõi, Driver/Lib, Toolchain CMake + Ninja + ST-Link CLI, Log UART & Thẻ nhớ.

---

## 📑 MỤC LỤC
1. [Tổng Quan Dự Án](#1-tổng-quan-dự-án)
2. [Sơ Đồ Kết Nối Phần Cứng & Pinout STM32](#2-sơ-đồ-kết-nối-phần-cứng--pinout-stm32)
3. [Thiết Kế Máy Trạng Thái Hữu Hạn (7-State FSM)](#3-thiết-kế-máy-trạng-thái-hữu-hạn-7-state-fsm)
4. [Kịch Bản Kiểm Thử Hoạt Động (Test Cases TC01 - TC10)](#4-kịch-bản-kiểm-thử-hoạt-động-test-cases-tc01---tc10)
5. [Thuật Toán Xử Lý Tín Hiệu & Hiệu Chuẩn Cảm Biến](#5-thuật-toán-xử-lý-tín-hiệu--hiệu-chuẩn-cảm-biến)
6. [Cấu Trúc Thư Mục Dự Án](#6-cấu-trúc-thư-mục-dự-án)
7. [Hướng Dẫn Biên Dịch (Build) & Nạp Code (Flash)](#7-hướng-dẫn-biên-dịch-build--nạp-code-flash)
8. [Hướng Dẫn Đọc Log Debug Qua UART](#8-hướng-dẫn-đọc-log-debug-qua-uart)

---

## 1. TỔNG QUAN DỰ ÁN

Hệ thống Báo động Xâm nhập là một giải pháp an ninh nhúng thời gian thực (Real-time Embedded Security System) được xây dựng trên vi điều khiển STM32F103C8T6. Hệ thống tích hợp đa cảm biến đầu vào nhằm bảo vệ toàn diện ngôi nhà/văn phòng chống lại các hành vi đột nhập trái phép:

* **Công tắc từ cửa (Reed Switch):** Giám sát trạng thái đóng/mở của cửa ra vào tức thì qua ngắt EXTI.
* **Cảm biến Rung (SW-420):** Thuật toán đếm xung trong cửa sổ trượt $1.0\text{s}$ để phân biệt rung nhẹ do gió/va quẹt với hành vi đập phá, dùng xà beng cạy cửa.
* **Cảm biến Thân nhiệt Chuyển động (PIR HC-SR501):** Phát hiện kẻ gian di chuyển trong vùng quét an ninh.
* **Bàn phím ma trận 4x4 (Keypad):** Nhập mã PIN bảo mật để Arm / Disarm / Bỏ qua cảnh báo.
* **Màn hình hiển thị OLED SH1106 / SSD1306 (I2C):** Trực quan hóa trạng thái hệ thống, đếm ngược thời gian, cấp độ rung và hướng dẫn người dùng.
* **Còi Báo động (Buzzer):** Phát âm thanh cảnh báo ngắt quãng hoặc còi hú khẩn cấp.
* **Thẻ nhớ MicroSD (SPI1 + FATFS):** Ghi nhật ký chi tiết mốc thời gian, loại cảm biến kích hoạt và sự kiện hệ thống.
* **Cổng Debug Serial (UART1):** Truyền log thời gian thực với tốc độ `115200 baud` lên máy tính.

---

## 2. SƠ ĐỒ KẾT NỐI PHẦN CỨNG & PINOUT STM32

Toàn bộ sơ đồ chân được cấu hình chuẩn trên STM32F103C8T6:

| Khối Chức Năng | Linh Kiện | Chân STM32 | Chế Độ Cấu Hình (GPIO/Peripheral) | Chức Năng Chi Tiết |
| :--- | :--- | :--- | :--- | :--- |
| **Cảm Biến Cửa** | Công tắc từ (MC-38 / Reed Switch) | **`PA0`** | `GPIO_EXTI0` (Pull-up, 2 sườn ngắt) | Đóng cửa = 0V, Mở cửa = 3.3V (Ngắt kích hoạt tức thì) |
| **Cảm Biến Thân Nhiệt** | Cảm biến chuyển động PIR (HC-SR501) | **`PA1`** | `GPIO_EXTI1` (Pull-down, Sườn lên) | Phát hiện người (Cấp nguồn VCC = 5V, Jumper chế độ **H**) |
| **Cảm Biến Rung** | Module rung SW-420 | **`PA2`** | `GPIO_EXTI2` (Pull-up, Sườn xuống) | Thu thập xung rung chấn động đập/cạy cửa |
| **Thẻ Nhớ (CS)** | Module MicroSD SPI | **`PA4`** | `GPIO_Output_PP` (Pull-up) | Chip Select (CS) điều khiển giao tiếp thẻ nhớ |
| **Thẻ Nhớ (SCK)** | Module MicroSD SPI | **`PA5`** | `SPI1_SCK` (Master, Baudrate Prescaler) | Xung nhịp đồng bộ truyền dữ liệu SPI1 |
| **Thẻ Nhớ (MISO)** | Module MicroSD SPI | **`PA6`** | `SPI1_MISO` (Master In Slave Out) | Dữ liệu từ thẻ SD gửi về STM32 |
| **Thẻ Nhớ (MOSI)** | Module MicroSD SPI | **`PA7`** | `SPI1_MOSI` (Master Out Slave In) | Dữ liệu từ STM32 ghi vào thẻ SD qua FATFS |
| **Còi Báo Động** | Active/Passive Buzzer | **`PA8`** | `GPIO_Output_PP` / TIM1_CH1 | Điều khiển tiếng bíp phím và còi hú báo động |
| **UART Debug (TX)**| Mạch nạp ST-Link / USB-UART | **`PA9`** | `USART1_TX` (115200 8N1) | Truyền log `printf` lên máy tính |
| **UART Debug (RX)**| Mạch nạp ST-Link / USB-UART | **`PA10`** | `USART1_RX` (115200 8N1) | Nhận lệnh điều khiển từ máy tính |
| **Bàn Phím (Hàng)** | Keypad 4x4 (Row 1..4) | **`PB0, PB1, PB10, PB11`** | `GPIO_Output_OD` (Pull-up) | Quét lần lượt từng hàng ma trận phím |
| **Bàn Phím (Cột)** | Keypad 4x4 (Col 1..4) | **`PB12, PB13, PB14, PB15`** | `GPIO_Input` (Pull-up) | Đọc trạng thái cột để giải mã phím bấm |
| **Màn Hình OLED** | OLED 1.3" SH1106 / 0.96" SSD1306 | **`PB6`** (SCL), **`PB7`** (SDA) | `I2C1_SCL`, `I2C1_SDA` (Fast Mode 400kHz) | Hiển thị giao diện UI đa màn hình |
| **LED Trạng Thái** | Onboard LED | **`PC13`** | `GPIO_Output_OD` (Active-Low) | Đèn báo nhịp tim hệ thống (Heartbeat 500ms) |

Module MicroSD 6 chân dùng trong dự án được nối theo thứ tự chức năng:

```text
Module CS   -> PA4       Module SCK  -> PA5
Module MOSI -> PA7       Module MISO -> PA6
Module VCC  -> nguồn 5 V thật
Module GND  -> GND chung với STM32 và USB-UART
```

Module dạng Catalex có AMS1117-3.3 phải được cấp vào chân `VCC` bằng nguồn 5 V
đã đo xác nhận; không cấp 3.3 V qua AMS1117 và không dùng chân `5VIN` chưa xác
minh là ngõ ra. Chi tiết đo kiểm và chẩn đoán nằm trong [`moduleSD.md`](moduleSD.md).

---

## 3. THIẾT KẾ MÁY TRẠNG THÁI HỮU HẠN (7-STATE FSM)

Hệ thống hoạt động dựa trên mô hình Máy trạng thái hữu hạn 7 trạng thái độc lập, khép kín và an toàn tuyệt đối:

```
                  ┌───────────────┐
       ┌─────────>│    DISARM     │<────────────────────────┐
       │          └───────┬───────┘                         │
       │                  │ (Nhập đúng PIN khi cửa đóng)    │
       │                  ▼                                 │
       │          ┌───────────────┐                         │
       │ (Hết giờ)│  EXIT DELAY   │                         │
       │ & Cửa mở │   (15 giây)   │                         │
       ├──────────┴───────┬───────┘                         │
       │                  │ (Hết 15s & Cửa đã đóng)         │
       │                  ▼                                 │
       │          ┌───────────────┐                         │
       │          │     ARMED     │<──────────────────┐     │
       │          └───┬───────┬───┘                   │     │
       │              │       │                       │     │
       │ (PIR / Rung) │       │ (Rung mạnh / Cửa mở)  │     │
       │              ▼       │                       │     │
       │      ┌───────────────┐│                      │     │
       │      │  ENTRY DELAY  ││                      │     │
       │      │   (30 giây)   ││                      │     │
       │      └───┬───────┬───┘│                      │     │
       │          │       │    │                      │     │
 (Đúng PIN)       │       │    │                      │     │
       │          │       │    │                      │     │
       ▼          ▼       │    │                      │     │
┌───────────────┐ (Hết 30s│    │                      │     │
│  TEMP DISARM  │ /Cạy cửa)│   │                      │     │
│   (60 giây)   │         │   │                      │     │
└───┬───────┬───┘         ▼   ▼                      │     │
    │       │     ┌───────────────┐                  │     │
    │       └────>│ ALARM EMERGE  │                  │     │
    │ (Hết 60s &  │  (Còi hú lớn) │                  │     │
    │  Cửa mở)    └───┬───────────┘                  │     │
    │                 │ (Nhập đúng PIN)              │     │
    │ (Hết 60s &      ▼                              │     │
    │  Cửa đóng)  ┌───────────────┐                  │     │
    └────────────>│  TEMP ALARM   │──────────────────┘     │
                  │   (30 giây)   │ (Hết 30s & Cửa đóng)   │
                  └───┬───────────┘                        │
                      │ (Nhập DISARM CODE)                 │
                      └────────────────────────────────────┘
```

### Chi tiết các trạng thái:
1. **`DISARM` (Giải trừ / Chờ):** Hệ thống không kích hoạt báo động. Cho phép người dùng nhập mã PIN kích hoạt chế độ bảo vệ.
2. **`EXIT DELAY` (Đếm ngược rời nhà - 15s):** Màn hình đếm lùi 15s, còi bíp nhịp chậm nhắc nhở. Người dùng có đủ thời gian bước ra ngoài và đóng cửa. Nếu hết 15s mà cửa vẫn mở, hệ thống hủy ARM và quay lại `DISARM`.
3. **`ARMED` (Vũ trang / Giám sát toàn diện):** Hệ thống giám sát chặt chẽ:
   * Nếu có người đi qua (PIR) hoặc Rung nhẹ $\rightarrow$ Chuyển sang `ENTRY DELAY`.
   * Nếu có Rung mạnh đập phá ($\ge 20$ xung) hoặc Cửa bị cạy mở $\rightarrow$ Nhảy thẳng sang `ALARM EMERGE`.
4. **`ENTRY DELAY` (Đếm ngược vào nhà - 30s):** Khi chủ nhà mở cửa bước vào, hệ thống bíp cảnh báo và đếm ngược 30s để nhập mã PIN. Nếu nhập đúng mã $\rightarrow$ Chuyển sang `TEMP DISARM`. Nếu quá 30s hoặc phát hiện cạy cửa bạo lực $\rightarrow$ Nhảy sang `ALARM EMERGE`.
5. **`TEMP DISARM` (Giải trừ tạm thời - 60s):** Cấp quyền 60s để bốc dỡ hàng hóa hoặc chuyển đồ vào nhà. Hết 60s: nếu cửa đã đóng $\rightarrow$ Tự động chuyển về `ARMED`; nếu cửa vẫn mở $\rightarrow$ Kích hoạt `ALARM EMERGE`.
6. **`ALARM EMERGE` (Báo động khẩn cấp):** Còi hú liên tục công suất lớn, ghi log báo động khẩn cấp vào Thẻ nhớ MicroSD, màn hình OLED nhấp nháy cảnh báo. Chỉ tắt khi nhập đúng mã PIN giải trừ.
7. **`TEMP ALARM` (Báo động tạm thời kiểm tra hiện trường - 30s):** Khi nhập mã trong trạng thái báo động, còi hạ âm lượng/bíp ngắt quãng trong 30s để chủ nhà vào kiểm tra hiện trường. Hết 30s: nếu cửa đã đóng $\rightarrow$ Tự động ARM lại; nếu cửa vẫn mở $\rightarrow$ Tái kích hoạt `ALARM EMERGE`.

---

## 4. KỊCH BẢN KIỂM THỬ HOẠT ĐỘNG (TEST CASES TC01 - TC10)

| Mã Test | Trạng Thái Bắt Đầu | Sự Kiện Kích Hoạt | Kết Quả Mong Đợi / Chuyển Trạng Thái |
| :--- | :--- | :--- | :--- |
| **TC01** | `DISARM` | Nhập đúng mã PIN khi cửa đóng | Vào `EXIT DELAY` (15s) $\rightarrow$ Hết 15s & Cửa đóng $\rightarrow$ Chuyển sang **`ARMED`**. |
| **TC02** | `DISARM` | Nhập đúng mã PIN nhưng cửa mở | Vào `EXIT DELAY` (15s) $\rightarrow$ Hết 15s cửa vẫn mở $\rightarrow$ Hủy ARM, về **`DISARM`**, OLED báo lỗi. |
| **TC03** | `EXIT DELAY` | Nhập mã PIN giải trừ | Hủy ngay chu trình đếm lùi $\rightarrow$ Trở về **`DISARM`**. |
| **TC04** | `ARMED` | Phát hiện PIR hoặc Rung nhẹ hợp lệ | Chuyển sang **`ENTRY DELAY`** (30s countdown), còi bíp nhắc nhở. |
| **TC05** | `ENTRY DELAY` | Nhập đúng mã PIN trước 30s | Chuyển sang **`TEMP DISARM`** (60s). |
| **TC06** | `ENTRY DELAY` | Hết 30s mà chưa nhập đúng PIN | Kích hoạt tức thì **`ALARM EMERGE`** (Còi hú toàn lực + Ghi thẻ SD). |
| **TC07** | `ENTRY DELAY` | Cửa bị mở toang hoặc Rung mạnh | Chuyển thẳng sang **`ALARM EMERGE`** ngay lập tức. |
| **TC08** | `TEMP DISARM` | Hết 60s và Cửa đã đóng lại | Tự động kích hoạt lại trạng thái **`ARMED`**. |
| **TC09** | `TEMP DISARM` | Hết 60s nhưng Cửa vẫn để mở | Kích hoạt **`ALARM EMERGE`** báo động quên đóng cửa. |
| **TC10** | `ALARM EMERGE` | Nhập đúng mã PIN giải trừ | Chuyển sang **`TEMP ALARM`** (30s kiểm tra). Hết 30s nếu cửa đóng $\rightarrow$ Về `ARMED`; nếu cửa mở $\rightarrow$ Quay lại `ALARM EMERGE`. |

---

## 5. THUẬT TOÁN XỬ LÝ TÍN HIỆU & HIỆU CHUẨN CẢM BIẾN

### 5.1. Cảm Biến Rung SW-420 (Module `sensors.c` & `sensors.h`)
* **Bộ lọc chống dội cơ khí (Glitch Filter):** Ngắt `EXTI2` áp dụng bộ lọc $8\text{ms}$ (`VIB_GLITCH_FILTER_MS = 8`) để loại bỏ hoàn toàn hiện tượng rung lò xo nội tại.
* **Cửa sổ trượt thời gian 1.0 giây (`VIB_WINDOW_MS = 1000`):** Đếm tổng số xung tích lũy trong 1 giây để phân loại:
  * **$0 - 4$ xung:** Nhiễu nền môi trường $\rightarrow$ Bỏ qua (`VIB_NONE`).
  * **$6 - 19$ xung:** Rung nhẹ do va quẹt, gõ cửa $\rightarrow$ **`VIB_LIGHT`**.
  * **$\ge 20$ xung:** Chấn động đập phá, dùng búa/xà beng cạy cửa $\rightarrow$ **`VIB_HEAVY`**.
* **Quy trình Hiệu chuẩn (Calibration):**
  1. Mở `Core/Inc/sensors.h`, đặt `#define CALIBRATION_MODE 1`.
  2. Nạp code và mở UART Terminal (`115200 baud`).
  3. Thử nghiệm các kịch bản: (1) Môi trường tĩnh $\rightarrow$ (2) Gõ cửa nhẹ $\rightarrow$ (3) Đập mạnh cạy cửa.
  4. Cập nhật các giá trị xung thu thập được vào `VIB_NOISE_MAX`, `VIB_LIGHT_MIN`, `VIB_HEAVY_MIN`.
  5. Đặt lại `#define CALIBRATION_MODE 0` và nạp bản chạy thực tế.

### 5.2. Công Tắc Từ Cửa (Reed Switch) & Logic Ghép Nối (Coupling Logic)
* Chống dội tiếp điểm cơ khí $50\text{ms}$ (`REED_DEBOUNCE_MS = 50`) trên `EXTI0`.
* **Logic thông minh kết hợp:**
  * **Cửa Đóng (`Reed == 0`):** Tự động gọi `Vibration_Reset()` xóa sạch chấn động lúc sập cửa $\rightarrow$ Bật chế độ giám sát rung.
  * **Cửa Mở (`Reed == 1`):** Ngắt phân tích rung để tránh hiện tượng gió lùa đập cánh cửa gây báo động rung giả.

### 5.3. Cảm Biến Chuyển Động Thân Nhiệt PIR (HC-SR501)
* **Thời gian Warm-up 30s:** Trong 30 giây đầu tiên khởi động (`PIR_WARMUP_MS = 30000`), hệ thống tự động khóa ngắt để đầu dò ổn định bề mặt nhiệt điện.
* **Cấu hình phần cứng bắt buộc:**
  * Cắm Jumper trên module sang vị trí **`H`** (Repeatable Trigger) để tín hiệu OUT giữ mức HIGH liên tục khi có người di chuyển.
  * Cấp nguồn VCC vào chân **`5V`** (không cắm 3.3V vì sẽ gây sụt áp IC ổn áp 7133).
  * Vặn chiết áp *Sensitivity* ngược chiều kim đồng hồ để giảm khoảng cách phát hiện xuống $2 - 3\text{m}$.
* **Thuật toán Khóa trạng thái (Hold Latch 1.5s):** Giữ trạng thái phát hiện tối thiểu 1.5 giây và tự động gia hạn khi người dùng vẫn đang chuyển động, loại bỏ hoàn toàn hiện tượng chớp tắt tín hiệu.

---

## 6. CẤU TRÚC THƯ MỤC DỰ ÁN

```text
Intrusion-Alarm-System/
├── CMakeLists.txt              # Cấu hình hệ thống biên dịch CMake
├── CMakePresets.json           # Thiết lập toolchain & preset build
├── Prj2008.ioc                 # File cấu hình đồ họa STM32CubeMX
├── STM32F103xx_FLASH.ld        # Linker script định vị bộ nhớ Flash/RAM
├── startup_stm32f103xb.s       # File khởi động Assembly của vi điều khiển
├── build_and_flash.bat         # Script tự động build & flash nhanh trên Windows
├── .gitignore                  # Bộ lọc loại bỏ file build trung gian
├── README.md                   # Tài liệu toàn diện của dự án
├── Core/
│   ├── Inc/                    # Các file Header khai báo (.h)
│   │   ├── main.h              # Định nghĩa chân I/O và nguyên mẫu hàm
│   │   ├── sensors.h           # Header driver phân loại rung & cảm biến
│   │   ├── keypad.h            # Header driver bàn phím ma trận 4x4
│   │   ├── ssd1306.h           # Header driver OLED SH1106 / SSD1306
│   │   ├── fonts.h             # Header phông chữ ma trận (Font 7x10, 11x18)
│   │   ├── gpio.h, usart.h...  # Khai báo cấu hình ngoại vi HAL
│   ├── Src/                    # Các file mã nguồn thực thi (.c)
│   │   ├── main.c              # Chương trình chính & Vòng lặp tác vụ
│   │   ├── sensors.c           # Hiện thực xử lý xung rung & lọc nhiễu
│   │   ├── keypad.c            # Quét ma trận phím Non-blocking
│   │   ├── ssd1306.c           # Điều khiển hiển thị OLED qua I2C
│   │   ├── fonts.c             # Bitmap font chữ ma trận
│   │   ├── gpio.c              # Cấu hình ngắt EXTI & chân I/O
│   │   ├── usart.c, spi.c...   # Khởi tạo phần cứng UART, SPI, Timer
├── FATFS/                      # Thư viện quản lý tệp FAT32 trên Thẻ nhớ SD
│   ├── App/fatfs.c
│   └── Target/user_diskio.c
├── Drivers/                    # Thư viện STM32F1xx HAL Driver & CMSIS
└── Middlewares/                # Mã nguồn FatFs Middleware
```

---

## 7. HƯỚNG DẪN BIÊN DỊCH (BUILD) & NẠP CODE (FLASH)

### Cách 1: Sử Dụng Script Tự Động (`build_and_flash.bat`)
Chạy file script `build_and_flash.bat` bằng cách nhấp đúp chuột hoặc gõ lệnh trong Windows Terminal:
```cmd
build_and_flash.bat
```
*Script sẽ tự động gọi CMake + Ninja để biên dịch và dùng STM32CubeProgrammer CLI để nạp trực tiếp qua ST-Link (chế độ Under Reset).*

### Cách 2: Sử Dụng Lệnh CMake & Ninja Thủ Công
1. **Cấu hình dự án (Chỉ chạy lần đầu):**
   ```bash
   cmake -B build -G "Ninja" -DCMAKE_TOOLCHAIN_FILE="cmake/gcc-arm-none-eabi.cmake"
   ```
2. **Biên dịch mã nguồn (Build):**
   ```bash
   cmake --build build
   ```
3. **Nạp Firmware (Flash qua ST-Link):**
   ```bash
   STM32_Programmer_CLI.exe -c port=SWD mode=UR -w build/Prj2008.elf -v -rst
   ```

---

## 8. HƯỚNG DẪN ĐỌC LOG DEBUG QUA UART

1. Cắm cáp chuyển đổi USB-to-UART (hoặc chân Virtual COM của ST-Link) vào máy tính:
   * Chân **`PA9`** (TX) của STM32 nối vào chân **`RX`** của mạch USB-to-UART.
   * Chân **`GND`** nối chung với GND máy tính.
2. Mở phần mềm Serial Monitor (PuTTY, Hercules, TeraTerm, hoặc Serial Monitor trên VS Code / STM32CubeIDE).
3. Thiết lập thông số kết nối:
   * **Baudrate:** `115200`
   * **Data bits:** `8`
   * **Parity:** `None`
   * **Stop bit:** `1`
   * **Flow Control:** `None`
4. Bạn sẽ nhận được các thông điệp hệ thống thời gian thực theo định dạng:
   ```text
   ========================================
     INTRUSION ALARM SYSTEM - STM32F103
     Firmware Ver 2.0 (Debounced & Classified)
     PIR Warm-up Time: 30 seconds...
   ========================================
   [KEYPAD] Pressed: 1
   [SENSOR] REED: Door CLOSED. Vibration monitoring active.
   [SENSOR] PIR: Warm-up Complete (30s). Motion monitoring ACTIVE!
   [12079ms] VIB window=8 level=LIGHT
   [14079ms] VIB window=23 level=HEAVY
   [SENSOR] PIR: Motion DETECTED!
   ```
