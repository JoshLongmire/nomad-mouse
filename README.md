# Nomad Mouse

A desk mouse. Waveshare 2 inch capacitive screen, Lonely Binary ESP32-S3 gold board, one joystick on a Lego mount. The stick sits where a MAX98357 amp used to be. The amp is gone.

It is a USB mouse, a handful of keyboard shortcuts, media keys, and an on-screen keyboard. It does not show Nomad status and it does not talk to the mesh.

## Two USB plugs

The board's own USB port is the mouse. After it boots, the PC sees a mouse and a keyboard. That port cannot take a new program.

The other cable is a CH340 (`1a86:7522`, QinHeng "USB Serial", usually `/dev/ttyUSB0`). That is the only programming port. Serial log is 115200. A resting stick looks like `X 1624 Y 1591 dx +0 dy +0 sw 0`.

PlatformIO's board name is `esp32-s3-devkitc-1`. That is the build target, not the silk on the gold board. Esptool on this chip reports ESP32-S3 QFN56 rev v0.2, 16 MB flash, 8 MB PSRAM. The MAC for this board is `14:c1:9f:da:3e:28`. Read the MAC before you write. Other ESP32 boards on the same PC will show up as serial ports too.

## Parts

| Part | What it does |
|---|---|
| Waveshare 2″ capacitive LCD | ST7789, 240×320 portrait. Touch is a CST816. |
| Lonely Binary ESP32-S3 | The chip. WS2812 on GPIO48. This firmware turns that LED off and leaves it off. |
| Joystick | Analog X/Y and a click, 3.3 V only. |

The module has an SD slot. This firmware does not use it. Settings stay on the chip, NVS namespace `joymouse`. A normal flash does not touch that partition, so Dead, Spd, Slow, center, invert, and the six tiles survive a reflash.

The pins match the Waveshare 2 inch ESP32-S3 capacitive table.

| Signal | GPIO |
|---|---|
| LCD SCLK / MOSI / MISO | 1 / 2 / 42 |
| LCD CS / DC / RST / backlight | 39 / 41 / 40 / 6 |
| Touch SDA / SCL / RST / INT | 15 / 7 / 16 / 17 |
| Stick X / Y / click | 4 / 5 / 8 |
| Stick VCC / GND | 3.3 V / GND |

The click is the stick switch to ground, with the chip's pull-up. Do not start the default Arduino `Wire` on GPIO8. That pin is the click.

The stick axes are swapped in firmware. There is no Swap button. Sideways on the stick moves the pointer up and down. Invert X and Invert Y are in Settings if one direction is backwards.

## Build and flash

80 MHz QIO dies in ROM (`ets_loader.c`) and the panel stays black, because the app never starts. Flash DIO, 40 MHz, 16 MB. The SDK here has no 40 MHz bootloader elf, so the build uses the 80 MHz DIO bootloader and the upload forces 40 MHz.

GFX is pinned to `moononournation/GFX Library for Arduino@1.4.9`. Newer GFX wants a header this core does not have.

On the CH340 cable: hold BOOT, tap RESET, let BOOT go.

```bash
pio run -e esp32-s3
```

`platformio.ini` points upload and monitor at the CH340 by-id path. If `pio run -t upload` is used, still pass DIO, 40 MHz, and 16 MB. The offsets are:

```text
0x0     bootloader.bin
0x8000  partitions.bin
0xe000  boot_app0.bin
0x10000 firmware.bin
```

`boot_app0.bin` is the Arduino-ESP32 copy at `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/`. Use PlatformIO's Python for esptool. The system Python on this machine does not have pyserial. Baud 460800.

After a good flash, unplug the CH340 and use the mouse plug. The PC has to see the board as a new mouse before a new media key works.

## Screen

Portrait, connector at the bottom. Touch matches that way up. Tan page, olive buttons, warm gray labels, a gray frame around the glass and around each button. Same colors as the Nomad 3.5 inch panel.

Home:

- Gear opens Settings. It is as tall as the Play / Vol+ / Vol− stack.
- Play, Vol+, Vol− are one column. Play is play/pause, once per tap. Volume repeats while held.
- Six macro tiles. A short tap sends the key. Hold about half a second, then drag, to swap two tiles, including empty ones.
- Keys, top right, opens the keyboard.
- Bottom row: Left, Scroll, Right.

The stick click is a left click at normal speed. Holding the on-screen Left button slows the pointer, including if Scroll is held or locked. Holding Right keeps the stick as a pointer at normal speed, so you can place the click.

Scroll: hold it and the stick scrolls. Let go and it is a mouse again. Two quick taps lock it. The button says Locked. A short tap releases the lock. A long hold does not lock or release it.

### Keyboard

Keys turns the picture clockwise. The right edge of the board is the top of the letters. The stick turns with it: push toward the top of the letters and the pointer goes up. Mouse, on the keyboard, puts the picture and the stick back.

Letters are tan on the olive keys. Sft capitalizes the next key. 123 opens numbers and the symbols used in passwords:

```text
! @ # $ % ^ & * ( ) - _ + = [ ] { } | \ / ? ` ~ ' " , . :
```

BS is backspace. Dict sends Ctrl+Shift+Space, the same shortcut as the Dictate tile.

### Settings

Olive bar at the top: Saved on chip (or Not saved yet), Dead, Spd, Slow. The bar stops above Set center.

| Control | What it does | Saved range |
|---|---|---|
| Set center | Rest the stick, then tap | Center |
| DZ − / DZ + | Deadzone | 20–400, step 20, default 50 |
| Spd − / Spd + | Pointer speed | 20–200%, step 10, default 100 |
| Slow − / Slow + | Speed while the on-screen Left button is held | 10–100%, step 10, default 25 |
| Inv X / Inv Y | Flip one axis | On or off |
| Macros / Back | Macro list, or home | |

The macro list scrolls. Tap a row to turn it on or off. Six at most, because the home screen has six tiles. Names only. The shortcut is not printed on the tile.

| Name | Key |
|---|---|
| Dictate | Ctrl+Shift+Space |
| Palette | Ctrl+Shift+P |
| Copy | Ctrl+C |
| Paste | Ctrl+V |
| Undo | Ctrl+Z |
| Save | Ctrl+S |
| Select all | Ctrl+A |
| Delete | Forward delete |
| Enter | Enter |
| Backspace | Backspace |
| Escape | Escape |
| Alt+Tab | Alt+Tab |

First boot, until you change them: Dictate, Palette, Copy, Paste, Select all, Delete.

## Black screen

If the backlight flashes once and the picture stays black, the app started and then something stole the SPI bus from the LCD. On this board that was the SD card, which shares the display's wires. This firmware does not mount a card. If an older image is still on the chip, flash this one again and leave the card alone.

If the CH340 log has `ets_loader.c` and no `X … Y …` lines, the chip is stuck in the 80 MHz QIO boot failure. Reflash DIO, 40 MHz, 16 MB, at the offsets above.
