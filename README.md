# T-SIMCAM → MediaMTX + dhome (ESP-IDF)

LilyGO **T-SIMCAM** (ESP32-S3 + OV5640) დაფისთვის:

- ცოცხალი MJPEG სტრიმი ბრაუზერით (`http://<ip>/`) — ლოკალური სანიტი-ჩეკი
- ლოკალური **RTSP/MJPEG სერვერი** პორტ 8554-ზე (LAN-ში პირდაპირ ნახულობ ffplay/VLC-თი)
- **RTSP push (publish)** დისტანციურ MediaMTX-ზე → ბრაუზერით ნახულობ HLS/WebRTC-ით
- **გარემოს სენსორები BluePill hub-ის გავლით**: SHT40 (T/RH) + VL53L1X (მანძილი) ეკიდება STM32F103 "BluePill"-ს, რომელიც აგრეგირებს და 1 Hz-ზე ერთ ASCII ხაზს უგზავნის ESP-ს UART-ით. ESP TCP-ით აწვდის dhome Go-სერვერს (`:5678`)
- **რელეს გამოყვანა (GPIO 21)** — ხელით HTTP-ით (`/relay/on|off|toggle`) ან ავტომატური თერმოსტატით (ტემპერატურა + დაკავებულობა)

## საჭიროა

- ESP-IDF **v5.1+** (`. $IDF_PATH/export.sh`)
- T-SIMCAM დაფა
- MediaMTX (სერვერი, ნებისმიერი მისამართით)

## აწყობა

```bash
cd /home/dato/Projects/LILIGO
idf.py set-target esp32s3
idf.py menuconfig    # → "T-SIMCAM Webcam" →
                     #     Wi-Fi SSID / Password (+ fallback 2/3)
                     #     MediaMTX host (default 198.211.114.79)
                     #     MediaMTX port (8554) / path (cam)
                     #     Sensors → dhome → BluePill link (UART) — RX=46
idf.py build flash monitor
```

**Wi-Fi (რამდენიმე ქსელი):** მთავარი SSID/Password-ის გარდა შეგიძლია ორი
სათადარიგო ქსელიც შეიყვანო (fallback 2/3). ESP თითო ქსელს `WIFI_MAX_RETRY`-ჯერ
ცდის, მერე გადადის შემდეგზე და ციკლურად უვლის ყველას. ცარიელი SSID-ი გამოტოვებულია.
`WIFI_MAX_CYCLES` სრული შემოვლის შემდეგ თუ ვერ დაუკავშირდა — ბორდი reboot-დება და
თავიდან იწყებს ძებნას.

წარმატებული ლოგი:

```
camera: PWR_EN (GPIO 1) high
camera: Detected OV5640 camera
wifi: connected to 'myssid', got IP: 192.168.x.x
http: HTTP server listening on port 80
rtsp: RTSP listening on :8554
rtsp-pub: connecting to rtsp://198.211.114.79:8554/cam
rtsp-pub: publishing (session=...)
bp_link: UART1 ready (TX=45, RX=46, 9600 baud)
bp_link: S:T=23.45 H=41.23 D=1238 O=0 ST=1 SV=1
sensors: connected to 198.211.114.79:5678
rtsp-pub: publish: 7.0 fps  (last jpeg 5600 bytes)
```

## ნახვა

### ლოკალურად (LAN)

| URL                                  | რა                       |
|--------------------------------------|--------------------------|
| `http://<esp-ip>/`                   | ბრაუზერი MJPEG          |
| `http://<esp-ip>/stream`             | multipart MJPEG          |
| `http://<esp-ip>/capture`            | ერთი JPEG               |
| `rtsp://<esp-ip>:8554/cam`           | RTSP/MJPEG (ffplay/VLC) |

### ნებისმიერი ადგილიდან (MediaMTX-ის გავლით)

ESP32 აგზავნის MJPEG-ს. ბრაუზერი HLS/WebRTC-ში MJPEG-ს ვერ ხედავს, ამიტომ
სერვერი ffmpeg-ით ცალკე path-ად აკეთებს H.264 ვერსიას.

| პროტოკოლი | URL                                          | კოდეკი |
|-----------|----------------------------------------------|--------|
| RTSP      | `rtsp://198.211.114.79:8554/cam`             | MJPEG  |
| RTSP      | `rtsp://198.211.114.79:8554/cam_h264`        | H.264  |
| HLS       | `http://198.211.114.79:8888/cam_h264`        | H.264  |
| WebRTC    | `http://198.211.114.79:8889/cam_h264`        | H.264  |

## MediaMTX სერვერ-მხარეს

წინაპირობა — ffmpeg ჩავიდეს:

```bash
sudo apt update && sudo apt install -y ffmpeg
```

გადააგდე `mediamtx.yml` სერვერზე და გაუშვი:

```bash
./mediamtx mediamtx.yml
```

რა ხდება:
1. ESP32 push-ით აგზავნის MJPEG-ს path-ზე `cam`
2. `runOnReady`-თ MediaMTX უშვებს ffmpeg-ს, რომელიც MJPEG-ს გადააქცევს
   H.264-ში და უკან აპუბლიკაცებს path-ზე `cam_h264`
3. ბრაუზერი ხედავს `cam_h264`-ს HLS/WebRTC-ით

ESP32-ის გათიშვისას ffmpeg ავტომატურად მოკვდება; ხელახლა შეერთებისას
თავიდან გაეშვება.

## სენსორები — BluePill hub

ESP32-S3-ზე თავისუფალი GPIO არ დარჩა (user header-ის პინების ნაწილი SPI flash /
PSRAM-ზე ზის — იხ. ქვემოთ), ამიტომ **ყველა სენსორი STM32F103 "BluePill"-ზე გადავიდა.**
BluePill კითხულობს ორივე სენსორს, აგრეგირებს და 1 Hz-ზე ერთ ASCII ხაზს უგზავნის
ESP-ს UART-ით. ESP მხოლოდ ამ ერთ ხაზს პარსავს (`bp_link.c`) — ახალი მავთული ESP-ის
მხარეს არ დასჭირვებია, რადგან იგივე UART პინები (45/46) გამოიყენა.

```
SHT40 ─UART(9600 "R:...")→ BluePill PA10 (USART1 RX)
VL53L1X ─I2C(PB6/PB7)────→ BluePill
                  │  აგრეგატორი 1 Hz
                  └─ PA2 (USART2 TX) ─UART(9600 "S:...")→ ESP GPIO 46 (UART1 RX)
```

### BluePill მხარე (STM32F103, `dhome_sens` პროექტი)

| ქსელი                    | BluePill პინი | შენიშვნა |
|--------------------------|----------------|----------|
| SHT40 TX → BluePill RX   | **PA10**       | USART1 RX, IT-ით ხაზობრივი პარსი |
| SHT40 RX                 | PA9            | USART1 TX, *გამოუყენებელი* (SHT40 მხოლოდ TX-ია) |
| BluePill TX → ESP RX     | **PA2**        | USART2 TX, აგზავნის `S:` ხაზს @ 1 Hz |
| VL53L1X SDA / SCL        | PB7 / PB6      | I2C1 |
| Heartbeat LED            | PC13           | თითო VL53L1X reading-ზე ციმციმებს |

SHT40 იკვებება BluePill-ის 3V3-დან. საჭიროა **საერთო GND** BluePill-ს, ESP-ს და
SHT40-ს შორის.

### ESP32 მხარე (T-SIMCAM)

| ქსელი                  | ESP პინი    | შენიშვნა |
|------------------------|-------------|----------|
| ESP RX ← BluePill TX   | **GPIO 46** | UART1 RX, `CONFIG_BP_LINK_RX_PIN` |
| ESP TX → BluePill RX   | GPIO 45     | UART1 TX, `CONFIG_BP_LINK_TX_PIN` — პრაქტიკაში გამოუყენებელი |
| UART port              | UART_NUM_1  | UART_NUM_0 = USB-Serial-JTAG console — არ ეხო |
| Baud                   | 9600        | `CONFIG_BP_LINK_BAUD` — BluePill USART2-ს უნდა ემთხვეოდეს |

### SHT40 მოდულის ნუანსი

edisonstore.ge-ს "SHT40"/"AHT30" მოდულები რეალურად **UART bridge-ებია** — შიდა MCU
სენსორს I2C-ით კითხულობს და UART-ით აგზავნის ASCII სტრიქონს (`R:041.6RH 024.7C\r\n`,
9600 8N1). silkscreen-ზე `+5V` წერია, მაგრამ onboard MCU 3.3V-ზე გადის. ამ
პროექტში მოდული პირდაპირ **BluePill-ს** ეკიდება, არა ESP-ს.

### Wire format (BluePill → ESP, 1 ხაზი/წამში)

```
S:T=23.45 H=41.23 D=1238 O=0 ST=1 SV=1\r\n
```

| ველი | მნიშვნელობა |
|------|-------------|
| `T`  | ტემპერატურა °C |
| `H`  | ტენიანობა % |
| `D`  | მანძილი mm (uint16) |
| `O`  | occupied 0/1 (`D < OCC_THRESHOLD`, default 500 mm — ითვლება BluePill-ზე) |
| `ST` | SHT40 freshness (1 = ხაზი < 5 წ-ის წინ დაიპარსა) |
| `SV` | VL53L1X freshness (1 = ბოლო read-ს `range_status == 0` < 5 წ-ის წინ) |

`bp_link_get_*()` accessor-ები `ESP_ERR_TIMEOUT`-ს აბრუნებენ თუ ხაზი 30 წ-ზე მეტი
დუმს ან შესაბამისი ST/SV ბიტი 0-ია. თერმოსტატიც და dhome JSON-იც ამ timeout-ებზე
ეყრდნობა.

### სამუშაო ციკლი

ESP ყოველ წამში (`CONFIG_SENSOR_PERIOD_MS`) კითხულობს `bp_link`-ის cached
მნიშვნელობებს და უგზავნის dhome-ს TCP-ით ერთ ხაზად JSON-ით:

```json
{"name":"tsimcam","temperature":23.4,"humidity":41.2,"distance_mm":1238,"occupied":false,"status":true}
```

`occupied` = `true` თუ მანძილი ზღვარს ქვემოთაა. `status` = `true` მხოლოდ თუ link
ცოცხალია და სენსორები fresh-ია.

`dhome` ინახავს ბოლო მნიშვნელობას, ხსნის HTTP API-ს (`/api/sensor`) და
"Smart Shelter" დაშბორდს `static/index.html`-ით (`:8080`).

**სერვერ-მხარეს** ფაიერვოლი დაუშვი:

```bash
ufw allow 5678/tcp   # ESP32 → dhome
ufw allow 8080/tcp   # ბრაუზერი → dhome dashboard
cd /home/dato/Projects/LILIGO/dhome && ./dhome
```

dashboard-ის HTML (`dhome/static/index.html`) Go ბინარშია **embed**-ით ჩამშენებული —
ცვლილების შემდეგ recompile სჭირდება:

```bash
cd dhome && GOOS=linux GOARCH=amd64 go build -o dhome
scp dhome root@198.211.114.79:~/dhome/
# სერვერზე: pkill dhome && cd ~/dhome && ./dhome &
```

## რელე (GPIO 21)

ერთი GPIO გამოყვანა relay-ის მოდულისთვის. ნაგულისხმევი პინი — **GPIO 21** (mPCIe LED line, broken-out, strapping-ის გარეშე).

**ხელით HTTP-ით:**

| URL | რა |
|-----|-----|
| `GET /relay` | მიმდინარე მდგომარეობა (`{"relay":"on"}` ან `off`) |
| `GET /relay/on` | ჩართე |
| `GET /relay/off` | გათიშე |
| `GET /relay/toggle` | შეცვალე |

**ავტომატური (თერმოსტატი):** `menuconfig → Relay output → [*] Drive relay automatically from temperature` + `MIN/MAX`. ცალკე FreeRTOS task-ში (`thermostat.c`), ტემპერატურასაც და **დაკავებულობასაც** (BluePill-ის მანძილის სენსორი) ითვალისწინებს:

- **ON** ⟺ `temp < MIN` **და** სახლი ნამდვილად ცარიელი არ არის
- **OFF** ⟺ `temp > MAX` **ან** სახლი ნამდვილად ცარიელია (დაცარიელდა — MAX-ს არ ელოდება)
- `MIN ≤ temp ≤ MAX` და დაკავებული/უცნობი → მდგომარეობა არ იცვლება (ჰისტერეზის ფანჯარა)

თუ მანძილის სენსორი არ პასუხობს (VL53L1X timeout/გაფუჭდა), occupancy "უცნობია" და
ლოგიკა **temp-only fallback-ზე** გადადის — ცივა → ირთვება, რომ მკვდარმა სენსორმა
ცხოველი ცივში არ დატოვოს. გადასვლები 3× debounce-ით ფილტრდება.

ნაგულისხმევი: MIN=10°C, MAX=12°C. მუშაობს მხოლოდ თუ Sensors → Enable sensor task ჩართულია (`SENSORS_ENABLED=y`). MAX უნდა იყოს > MIN, თორემ რელე იოსცილირებს.

**active-high vs active-low:** ბევრი ოპტოიზოლირებული რელე-მოდული active-low-ია (LOW = ჩართული). menuconfig → Relay output → [*] Relay is active-low — ცვლის სიგნალის პოლარობას.

## პროექტის სტრუქტურა

```
.
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── mediamtx.yml              # server-side template (camera transcoding)
├── dhome/                    # Go server (TCP + HTTP dashboard)
│   ├── main.go
│   └── static/index.html
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml     # espressif/esp32-camera
│   ├── Kconfig.projbuild     # SSID/PASS + MediaMTX + sensor + relay cfg
│   ├── camera_pins.h         # T-SIMCAM pinout + PWR_EN
│   ├── camera.{c,h}          # sensor init + PWR_EN sequence
│   ├── wifi_sta.{c,h}        # Wi-Fi STA
│   ├── http_server.{c,h}     # / + /stream + /capture + /relay*
│   ├── rtp_mjpeg.{c,h}       # JPEG parse + RFC 2435 packetizer
│   ├── rtsp_server.{c,h}     # local RTSP/UDP
│   ├── rtsp_publisher.{c,h}  # PUSH to remote MediaMTX
│   ├── relay.{c,h}           # GPIO output (default GPIO 21)
│   ├── bp_link.{c,h}         # BluePill UART line parser + cached accessors
│   ├── thermostat.{c,h}      # auto-relay task (temp + occupancy)
│   ├── sensor_task.{c,h}     # periodic bp_link read + TCP JSON to dhome
│   └── main.c
└── README.md
```

## T-SIMCAM-სპეციფიკური ნუანსები

ოფიციალური წყარო:
[Xinyuan-LilyGO/LilyGo-Camera-Series](https://github.com/Xinyuan-LilyGO/LilyGo-Camera-Series)
(`docs/T_SIMCAM.md`-ში).

- **არ აურიო** `LilyGo-Cam-ESP32S3` რეპოს პინაუტი — ის სხვა პროდუქტისთვისაა
  (T-Camera-S3) და SCCB ცარიელი დარჩება T-SIMCAM-ზე
- `PWR_ON_PIN = GPIO 1` უნდა აიყვანო HIGH-ზე *კამერის init-ამდე*, თორემ
  სენსორი მკვდარია
- `RESET = -1` უსაფრთხოა ორივე რევიზიაზე (V1.2/V1.3): V1.3-ში GPIO 18
  IR-cut ფილტრისთვის გადანაცვლდა
- **GPIO 26–32 = SPI flash, GPIO 33–37 = OPI PSRAM (R8 ვარიანტი).** ნებისმიერი
  მათგანის user-GPIO-დ გამოყენება ბორდს აგდებს boot-panic-ში. LilyGo-ს silkscreen
  "IO31"-თან მცდარია. სწორედ ამიტომ გადავიდა ყველა სენსორი BluePill hub-ზე — ESP-ის
  user header-ზე თავისუფალი, უსაფრთხო GPIO აღარ რჩებოდა
