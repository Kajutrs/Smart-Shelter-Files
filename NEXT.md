# სად დავრჩი — გავაგრძელოთ ხვალ

სრული დოკუმენტაცია → `README.md`. ეს ფაილი მოკლე ხსოვნაა შემდეგი სესიისთვის.
**ეს ფაილი იკითხება `bluepill-sensor-hub` ბრენჩზე.** main-ში ჯერ არ არის შერწყმული.

- LILIGO repo, branch `bluepill-sensor-hub`, ბოლო commit `2fff5ba` (2026-06-10 README rewrite)
- dhome_sens repo, branch `sensor-hub`, ბოლო commit `13d0486`

ორივე ბრენჩი ჰარდვერზე გადამოწმდა; multi-SSID Wi-Fi-ც გადამოწმდა (2026-06-10). მერჯი ჯერ კიდევ გადადებულია — user თვითონ გააკეთებს ორივე repo-ში.

## რა მუშაობს (2026-05-24 ბოლოს)

| ეტაპი | მდგომარეობა |
|-------|--------------|
| HTTP MJPEG webcam | ✅ `http://<esp-ip>/` |
| ლოკალური RTSP server | ✅ `rtsp://<esp-ip>:8554/cam` |
| RTSP push → MediaMTX (DO) | ✅ WebRTC `:8889/cam_h264` |
| dhome dashboard | ✅ `http://198.211.114.79:8080/` — ახალი "Smart Shelter" frontend (2026-05-30) |
| Relay (GPIO 21) + 2N2222A + opto | ✅ HTTP + auto-thermostat |
| Auto-thermostat | ✅ ცალკე task, ეხლა bp_link-დან კითხულობს temp-ს |
| **BluePill sensor hub → ESP UART** | ✅ **ახალი (დღევანდელი)** |
| - SHT40 → BluePill USART1 (PA10 RX) | ✅ |
| - VL53L1X → BluePill I2C (PB6/PB7) | ✅ |
| - BluePill USART2 TX (PA2) → ESP GPIO 46 | ✅ 1 Hz, 9600 baud |

## დღევანდელი ისტორია მოკლედ

დილით დავიწყეთ "BluePill VL53L1X → ერთი GPIO → ESP IO31"-ის გეგმით. შუა დღეს აღმოვაჩინეთ რომ **IO31 ESP32-S3-ის SPI ფლეშის რეზერვირებული პინია** (GPIO 26-32 ყველა flash bus-ზე ზის) — გამოყენებამ ESP გადააგდო TG1WDT panic-ის ციკლში. LilyGo-ის silkscreen-ი არასწორად მონიშნავს როგორც თავისუფალს, მაგრამ პინი unusable-ია.

რადგან user header-ზე სხვა თავისუფალი GPIO არ რჩება, არქიტექტურა გადავიტანე: **ყველა სენსორი BluePill-ზე, ESP მხოლოდ ერთ UART ხაზს კითხულობს.** ეგ wire უკვე გვქონდა (SHT40-ის ხაზი GPIO 45/46), ანუ ერთი ახალი მავთულიც კი არ დაგვჭირდა ESP-ის მხარეს.

## არქიტექტურა

```
SHT40 ─UART(9600 R:...)→ BluePill PA10 (USART1 RX)
VL53L1X ─I2C(PB6/PB7)→ BluePill
                  │
                  └─ აგრეგატორი 1 Hz-ზე →
                  PA2 (USART2 TX) ─UART(9600 S:...)→ ESP GPIO 46 (UART1 RX)
```

Wire format BluePill → ESP:
```
S:T=23.45 H=41.23 D=1238 O=0 ST=1 SV=1\r\n
```
ST=1 → SHT40 line < 5s old; SV=1 → VL53L1X range_status==0 < 5s old. სრული დეტალები → memory `reference-bp-link`.

## ფაილების ცვლილებები (`bluepill-sensor-hub` branch, commit `4a2809e`)

ESP-ის მხარე:
- ➕ `main/bp_link.{c,h}` — UART line parser + cached accessors
- ➖ `main/sht40.{c,h}`, `main/vl53l1x.{c,h}`, `main/i2c_bus.{c,h}`, `main/occupancy.{c,h}` — წავიდა, BluePill-მა ჩაიბარა
- 🔧 `main/sensor_task.c` — 3 სენსორის ცემვების ნაცვლად ერთი bp_link გამოძახება
- 🔧 `main/thermostat.c` — `bp_link_get_temp_humidity()`
- 🔧 `main/Kconfig.projbuild` — SHT40/VL53L1X/Occupancy → ერთი "BluePill link (UART)" menu
- 🔧 `main/CMakeLists.txt`, `main/main.c` — gate-ების განახლება

BluePill-ის მხარე (`sensor-hub`, commit `13d0486`):
- 🔧 `Core/Src/main.c` — USART1+USART2 init, SHT40 IT line parser, 1 Hz aggregator
- 🔧 `Core/Inc/stm32f1xx_hal_conf.h` — `HAL_UART_MODULE_ENABLED`
- ➕ `Drivers/STM32F1xx_HAL_Driver/{Src,Inc}/stm32f1xx_hal_uart.{c,h}` — ვენდორდი ხელით (CubeMX არ რეგენ-ცემს)

## 2026-05-30 — dhome frontend გადაკეთება

ძველი მუქი dashboard შეიცვალა დიზაინირებული **"Smart Shelter for Street Animals"** გვერდით.

- 🔧 `dhome/static/index.html` — ახალი landing page (nav / hero / Live Feed 4 cam / Stats / Donate / Smart Shelter info / footer), სრული EN/GE i18n. `/api/sensor` polling 3 წ-ში — Temperature / Distance / Status. (Humidity აღარ ჩანს, თუმცა backend ისევ აგზავნის.)
- 🔧 `dhome/main.go` — `fs.Sub(staticFiles, "static")`, რომ `/` პირდაპირ index.html-ს აწვდიდეს (იყო directory listing; გვერდი მხოლოდ `/static/`-ზე).
- **Cam N1** = ცოცხალი WebRTC `<iframe>` (`:8889/cam_h264/`). **N2–N4** = "No data" base64 placeholder-ები (ერთი კამერა სტრიმავს).
- commits: `38017a5` (frontend), `7204256` (gitignore).
- `dhome/hero-section.html` (~23MB დიზაინის წყარო) და `dhome.zip` → gitignored. `static/index.html` (~20MB) მისი derived served ასლია.
- ⚠️ **index.html / hero-section.html Read/Edit-ით ვერ იხსნება (20+MB).** ხაზის ნომრით awk/sed-ით რედაქტირდა. რეალურ frontend მუშაობამდე base64 სურათების ცალკე ფაილებად ამოღება გვირჩევია (ემთხვევა embed→filesystem გეგმას).
- deploy: მხოლოდ `dhome` ბინარი (index.html ჩაშენებულია embed-ით) — `go build -o dhome .` → `scp` → restart.

Status card-ის "Heating On/Off" `data.status`-ით იწერება (= სენსორების სიჯანსაღე), არა რეალური relay state-ით — dhome-ის `SensorData`-ში relay ველი არ არის. თუ ნამდვილი heating ჩვენება გვინდა, ESP-მ relay state უნდა დაამატოს JSON-ში.

## 2026-06-10 — multi-SSID Wi-Fi + occupancy thermostat + README

სამი ახალი commit `bluepill-sensor-hub`-ზე, `09dc557` (frontend)-ის თავზე:

- `d6679b3` **wifi: try multiple SSIDs with fallback** — ერთი hardcoded SSID-ის
  ნაცვლად 3 ქსელი (`WIFI_SSID` + `WIFI_SSID_2/3`, ცარიელი = გამოტოვება). თითო ქსელი
  `WIFI_MAX_RETRY`-ჯერ, მერე შემდეგი, ციკლურად; `WIFI_MAX_CYCLES` სრული შემოვლის
  შემდეგ → ESP_FAIL → reboot → თავიდან. ჰარდვერზე გადამოწმდა. ფაილები:
  `main/wifi_sta.c` (runtime cred array, `wifi_apply_cred()`), `main/Kconfig.projbuild`.
- `53b782b` **thermostat: gate heating on occupancy** — `relay ON` ახლა მოითხოვს
  `temp < MIN`-საც **და** რომ სახლი ნამდვილად ცარიელი არ იყოს. დაცარიელებაზე
  ირთვება OFF (MAX-ს არ ელოდება). occupancy `bp_link_get_occupied()`-დან; თუ
  მანძილის სენსორი timeout-ია → temp-only fallback (ცივა → ON), რომ მკვდარმა
  ToF-მა ცხოველი ცივში არ დატოვოს. 3× debounce. user-ის გადაწყვეტილებები: unknown
  → heat; vacated → off. ფაილი: `main/thermostat.c` (`on_hits`/`off_hits` მთვლელები).
- `2fff5ba` **docs: rewrite README for BluePill sensor hub architecture** — README
  ძველ (ESP პირდაპირ კითხულობს SHT40/VL53L1X) layout-ს ასახავდა; გადაიწერა BluePill
  hub-ისთვის + multi-SSID + occupancy thermostat + GPIO 26-37 flash/PSRAM trap.

## დარჩენილი ნაბიჯები

**მოკლევადიანი:**
- frontend-ზე მუშაობის გაგრძელება (user-ის გადაწყვეტილებით)
- ორივე branch-ის merge main-ში (fast-forward ორივეგან) — **user თვითონ გააკეთებს**;
  frontend + დღევანდელი commit-ები ყველა `bluepill-sensor-hub`-ზეა

**უფრო შორს:**
- base64 → ცალკე ფაილები + dhome embed-დან filesystem-ზე გადატანა (ცვლილებაზე recompile სჭირდება, ცუდი loop)
- Relay manual-override timeout
- MediaMTX/dhome auth
- Sensor history persistence

## დღევანდელი გაკვეთილები

1. **ESP32-S3 GPIO 26-32 = SPI flash. GPIO 33-37 = OPI PSRAM (R8 variant). ნებისმიერი მათგანი როგორც user GPIO = crash.** LilyGo-ის silkscreen "IO31"-თან მცდარია. ნებისმიერი ESP32-S3 ბორდის pinout-ი ჯერ Espressif-ის datasheet-ის "reserved pins" სიასთან გადაამოწმე.
2. **VL53L1X-ის ლენზაზე ფირი** იწვევს "always close" reading-ს. ToF სენსორზე ჯერ ფირი/ლენტი შემოწმდე ვიდრე firmware-ის ბაგებს დაუწყებ ძებნას.
3. **BluePill clone ST-Link + NRST wire-ის გარეშე** → CubeIDE debug ვერ უერთდება. flash-only workflow CubeProgrammer-ით (BOOT0 jumper trick) საკმარისია — live debugger-ში ბრძოლა ცემვა.

## Flash workflow (გადასახსოვრად)

**ESP** (`bluepill-sensor-hub` checkout-ში):
```
. $IDF_PATH/export.sh
idf.py build flash monitor
```

**BluePill** (`sensor-hub` checkout-ში):
- CubeIDE-ში open project → მარჯვენა click → Refresh (F5) → Build
- ან CLI-ით: `cd Debug && make all -j$(nproc)` (PATH=/opt/st/stm32cubeclt_*/GNU-tools-for-STM32/bin)
- Flash: BOOT0=1 → RESET → CubeProgrammer → Open `Debug/dhome_sens.elf` → Download → BOOT0=0 → RESET
