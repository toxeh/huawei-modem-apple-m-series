# Huawei Modem Tools for Apple Silicon (M-Series)

Universal USB tools for Huawei 3G/4G/LTE modems on macOS with Apple Silicon processors.

## Requirements

### For running pre-compiled binaries:
```bash
# Install Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install libusb (required runtime dependency)
brew install libusb
```

### For compiling from source:
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install libusb
brew install libusb
```

## Tools

### `huawei_at` - AT Command Interface
Send AT commands to Huawei modems in modem mode.

```bash
# List available devices
./bin/huawei_at -l

# Send AT command (auto-detect device)
./bin/huawei_at "AT"
./bin/huawei_at "AT+CPIN?"
./bin/huawei_at "ATI"

# Force specific PID
./bin/huawei_at -p 1506 "ATI"

# Verbose mode
./bin/huawei_at -v "AT+CSQ"
```

### `huawei_modeswitch` - Mode Switcher
Switch Huawei modems from ZeroCD/Storage mode to Modem mode.

```bash
# List devices
./bin/huawei_modeswitch -l

# Auto-switch any ZeroCD device found
./bin/huawei_modeswitch

# Force specific PID
./bin/huawei_modeswitch -p 14fe
```

## Building

```bash
# macOS with Homebrew
brew install libusb
mkdir -p bin
clang -o bin/huawei_at huawei_at.c -I/opt/homebrew/include -L/opt/homebrew/lib -lusb-1.0
clang -o bin/huawei_modeswitch huawei_modeswitch.c -I/opt/homebrew/include -L/opt/homebrew/lib -lusb-1.0
```

## Supported Devices

### ZeroCD Mode (need switching)
| PID | Model |
|-----|-------|
| 0x1446 | E1550/E1756/E173 ZeroCD |
| 0x14fe | E303/E3131/E1550 Intermediate |
| 0x1505 | E3131/E398 ZeroCD |
| 0x1520 | K3765 ZeroCD |
| 0x1521 | K4505 ZeroCD |
| 0x14D1 | E173 ZeroCD |
| 0x1c0b | E3531/E173s ZeroCD |
| 0x1f01 | E3131/E353/E3372/E8372 ZeroCD |
| 0x1da1 | E3372 ZeroCD |
| 0x1f1e | K5160 ZeroCD |
| 0x15ca | E3131h-2 ZeroCD |
| 0x1575 | K5150 ZeroCD |
| 0x157c | E3276 ZeroCD |
| 0x1582 | E8278 ZeroCD |
| 0x1588 | E3372 variant ZeroCD |
| 0x15b6 | E3331 ZeroCD |

### Modem Mode (ready to use)
| PID | Model | Type |
|-----|-------|------|
| **Classic 3G/HSPA** |||
| 0x1001 | E169/E620/E800/E1550 | HSDPA |
| 0x1003 | E1550 | HSPA |
| 0x140c | E180/E1550 | HSPA |
| 0x1406 | E1750 | HSPA |
| 0x1436 | E173/E1750 | HSPA |
| 0x1465 | K3765 | HSPA |
| 0x14AC | E1820 | HSPA |
| 0x14C6 | K4605 | HSPA+ |
| 0x14C9 | K4505 | HSPA+ |
| 0x1c05 | E173 | HSPA |
| 0x1c07 | E173s | HSPA |
| 0x1c1b | E3531 | HSPA |
| **E3xx Series** |||
| 0x1506 | E303/E3131/MS2372 | Modem |
| 0x14db | E3131/E353 HiLink | NCM |
| 0x15ca | E3131h-2 | Modem |
| **LTE Series** |||
| 0x1442 | E3372 Stick | LTE Modem |
| 0x14dc | E3372/E8372 HiLink | LTE NCM |
| 0x155e | E8372 NCM | LTE WiFi |
| 0x157f | E8372 | LTE Alt |
| 0x1592 | E8372h | LTE WiFi |
| 0x1573 | K5150 | LTE Modem |
| 0x1576 | K5160 | LTE Modem |
| 0x15c1 | ME906s | LTE M.2 |
| **Legacy** |||
| 0x1404 | E1752 | 3G |
| 0x1411 | E510 | 3G |
| 0x1464 | K4510/K4511 | 3G |

## Troubleshooting

### Device not detected
1. Check USB connection
2. Try `./huawei_modeswitch -l` to list devices
3. If in ZeroCD mode, run `./huawei_modeswitch` first

### Permission denied
```bash
sudo ./huawei_at "ATI"
```

### Unknown PID
Use force mode:
```bash
./huawei_at -p XXXX "ATI"
```

## License

MIT License
