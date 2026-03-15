# RF Waterfall Spectrogram
### M5Stack CardputerADV + SX1262 LoRa Addon | ESP-IDF C

A real-time RF waterfall spectrogram that sweeps 862–928 MHz in a 5 MHz span,
displaying signal strength utilizing the sx1262 rf chip.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | M5Stack CardputerADV (ESP32-S3) |
| Radio | SX1262 LoRa addon (SPI) |
| Display | ST7789V2 1.14" 240×135 (built-in) |
| Keyboard | I2C keyboard controller (built-in) |

### Pin Mapping (edit `main/board_config.h` if yours differs)

| Signal | GPIO |
|--------|------|
| LCD MOSI | 35 |
| LCD CLK | 36 |
| LCD CS | 37 |
| LCD DC | 34 |
| LCD RST | 33 |
| LCD BL | 38 |
| SX1262 MISO | 39 |
| SX1262 NSS | 9 |
| SX1262 RESET | 8 |
| SX1262 BUSY | 3 |
| SX1262 DIO1 | 46 |
| KB SDA | 13 |
| KB SCL | 15 |




---

## Keyboard Controls

| Key | Action |
|-----|--------|
| `9` / `del` | Tune center -250 kHz |
| `K` / `\` | Tune center +250 kHz |
| `Y` | Tune center +1 MHz |
| `O` | Tune center -1 MHz |
| `F` | Raise RSSI ceiling (show weaker signals) |

---

## Display Layout

```
┌──────────────────────────────────────┐  ← Row 0
│  895.0MHz   5000KHz span             │  Header (16px)
├──────────────────────────────────────┤  ← Row 16
│                                      │
│   [  Waterfall Spectrogram  ]        │  119 rows × 240 cols
│   Newest scan at top, scrolls down   │
│                                      │
└──────────────────────────────────────┘  ← Row 134
  862MHz         895MHz         928MHz
```

### Color Scale (thermal palette)
```
Black → Blue → Cyan → Green → Yellow → Red → White
 weak                   RSSI                  strong
```

---

## How It Works

1. The SX1262 is configured in LoRa mode (SF7, BW 500kHz) for RSSI measurement.
2. Each scan sweeps 240 frequency steps across the 5 MHz span (~20.8 kHz/step).
3. At each step: set frequency → lock PLL → brief RX → read `GetRssiInst`.
4. RSSI values are normalized to 0–255 and mapped to a thermal color palette.
5. Each completed sweep is pushed as a new row at the top of the waterfall,
   with older rows scrolling down.
6. Full display redraws happen every 30 scans to prevent display artifacts.

---

## Tuning

Edit `main/board_config.h` to adjust:
- `RF_DEFAULT_CTR_HZ` — starting center frequency
- `RF_SPAN_HZ` — scan span in Hz
- `RF_MIN_FREQ_HZ` / `RF_MAX_FREQ_HZ` — tuning limits
- `HEADER_HEIGHT` — pixels reserved for the info bar
