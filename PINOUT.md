# PINOUT — LILIGO Smart Shelter (T-SIMCAM + BluePill sensor hub)

ბრენჩი: `bluepill-sensor-hub`. ბოლო ვერიფიკაცია კოდთან: 2026-06-06.

არქიტექტურა: **BluePill (STM32F103) ფლობს ყველა სენსორს** (SHT40 + VL53L1X),
აგროვებს და ერთ ASCII ხაზს უგზავნის ESP32-ს (T-SIMCAM) UART-ით.
ESP აკეთებს კამერას (RTSP→MediaMTX) და რელეს, სენსორებს მხოლოდ კითხულობს.

```
 SHT40 (UART module)        VL53L1X (I2C)
        │ TX                   │ SDA/SCL
        ▼                      ▼
   ┌──────────────────────────────┐
   │      BluePill STM32F103       │  ← სენსორ-ჰაბი (თავისი USB კვებით)
   │  USART1: SHT40   I2C1: VL53   │
   └──────────────┬───────────────┘
        PA2 (USART2 TX) │  9600 baud, ASCII line @1Hz
                        ▼
                  GPIO46 (UART1 RX)
   ┌──────────────────────────────┐
   │     ESP32-S3  (T-SIMCAM)      │
   │  Camera + Relay + bp_link     │
   └──────────────────────────────┘
        │ RTSP push            │ TCP JSON :5678
        ▼                      ▼
     MediaMTX              dhome (Go)     ← DO droplet 198.211.114.79
```

---

## 1. BluePill ↔ ESP32 link (UART)

| Net                    | BluePill pin | ESP32 pin   | ESP Kconfig            |
|------------------------|--------------|-------------|------------------------|
| BluePill TX → ESP RX   | **PA2** (USART2 TX) | **GPIO 46** (UART1 RX) | `BP_LINK_RX_PIN=46`    |
| ESP TX → BluePill RX   | PA3 (USART2 RX) *unused* | GPIO 45 (UART1 TX) *unused* | `BP_LINK_TX_PIN=45`    |
| GND                    | GND          | GND         | — საერთო GND სავალდებულო |

- UART port: `UART_NUM_1` (`BP_LINK_UART_NUM=1`). UART0 = USB-Serial-JTAG console, არ გამოიყენო.
- Baud: **9600** 8N1 (`BP_LINK_BAUD=9600`), უნდა ემთხვეოდეს BluePill USART2-ს.
- GPIO 46/45 = T-SIMCAM-ის mPCIe RX/TX ხაზები, 6-pin user header-ზე გამოყვანილი.

### Wire format (BluePill → ESP, 1 ხაზი/წმ)
```
S:T=23.45 H=41.23 D=1238 O=0 ST=1 SV=1\r\n
```
- `T` ტემპ °C · `H` ტენიანობა % · `D` მანძილი mm · `O` occupied 0/1
- `ST` SHT40 freshness (1=ბოლო 5წმ-ში OK) · `SV` VL53L1X freshness
- ESP parse: `sscanf("S:T=%f H=%f D=%u O=%u ST=%u SV=%u")`; >30წმ სიჩუმეზე → ERR_TIMEOUT.

---

## 2. BluePill სენსორების მხარე (STM32F103)

| Net                      | BluePill pin | Notes |
|--------------------------|--------------|-------|
| SHT40 TX → BluePill RX   | **PA10** (USART1 RX) | IT-driven line parse |
| SHT40 RX                 | PA9 (USART1 TX) *unused* | SHT40 module TX-only; დატოვე disconnected |
| VL53L1X SDA              | **PB7** (I2C1) | |
| VL53L1X SCL              | **PB6** (I2C1) | |
| Heartbeat LED            | PC13         | toggles each VL53L1X read (~10Hz) |
| PA0                      | (free)       | ძველი single-GPIO occupied output, აღარ გამოიყენება |

- SHT40 module wire-format: `R:041.6RH 024.7C\r\n` (9600 8N1, streams unprompted).
- ⚠️ "SHT40" სინამდვილეში **UART bridge module**-ია, არა raw I2C. სილკზე `+5V` წერია,
  მაგრამ კვება **3V3**-ით (onboard MCU 3.3V).
- SHT40 იკვებება BluePill-ის 3V3-დან; საერთო GND ყველას შორის.

---

## 3. ESP32 T-SIMCAM — რელე

| Net   | ESP32 pin | Kconfig | Notes |
|-------|-----------|---------|-------|
| Relay | **GPIO 21** | `RELAY_GPIO=21` | mPCIe LED ხაზი, user header-ზე |

- `RELAY_ACTIVE_LOW=n` — opto-isolated module + 2N2222A NPN inverter (base 3.3kΩ);
  NPN-ის ინვერსია აბათილებს opto-ს active-low-ს → net active-high.
- ავტო-თერმოსტატი: `RELAY_AUTO_TEMP`, MIN=10°C ON / MAX=12°C OFF (hysteresis).
  ცხოვრობს ცალკე FreeRTOS task-ში (`thermostat.c`), არა sensor_task-ში.

---

## 4. ESP32 T-SIMCAM — კამერა (OV5640), `main/camera_pins.h`

| Signal | GPIO | Signal | GPIO |
|--------|------|--------|------|
| PWR_EN | **1** (HIGH რომ ჩაირთოს) | PWDN | -1 |
| RESET  | -1 (V1.2=18 / V1.3 IR-cut) | XCLK | 14 |
| SIOD (SDA) | 4 | SIOC (SCL) | 5 |
| Y9 | 15 | Y8 | 16 |
| Y7 | 17 | Y6 | 12 |
| Y5 | 10 | Y4 | 8 |
| Y3 | 9 | Y2 | 11 |
| VSYNC | 6 | HREF | 7 |
| PCLK | 13 | | |

---

## 5. T-SIMCAM user header (6 pin) + აკრძალული GPIO-ები

Header (top→bottom): `IO31 | IO46 | IO45 | IO21 | GND | 3V3`
- IO46 → BluePill RX · IO45 → BluePill TX · IO21 → რელე · 3V3/GND → off-board კვება.
- ⚠️ **IO31 = ხაფანგი** — flash bus (SPIQ). output-ად კონფიგი → boot crash. არ გამოიყენო.
- ⚠️ **GPIO 26–37 ყველა off-limits** ESP32-S3R8-ზე: 26–32 = SPI flash, 33–37 = Octal PSRAM.
- header-ზე თავისუფალი signal pin აღარაა; ახალი პერიფერია → mPCIe (GPIO48), SD pads
  (38/39/40/47), mic pads (2/41/42), ან Grove.

---

## სტატუსი
- BluePill სენსორ-ჰაბის არქიტექტურა ჯერ `main`-ში არ არის ჩამერჯული
  (LILIGO `bluepill-sensor-hub`, dhome_sens `sensor-hub`). ჰარდვეარზე ვალიდაციის შემდეგ ჩაიმერჯება.
- VL53L1X **არ ჩართო** (`VL53L1X_ENABLED`) სანამ ფიზიკურად არ არის მიერთებული — I2C-NACK storm.
</content>
</invoke>
