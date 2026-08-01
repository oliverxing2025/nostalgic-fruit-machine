<div align="center">
  <h1>Nostalgic Fruit Machine</h1>
  <p><strong>Nostalgic Fruit Machine for M5Stack StickS3</strong></p>
  <p>
    A pocket-sized retro arcade with motion controls,<br>
    running lights, risk play, and a 30-gem collection.
  </p>
  <p>
    <a href="#overview">Overview</a> ·
    <a href="#whats-new-in-v063">v0.6.3</a> ·
    <a href="#complete-controls">Controls</a> ·
    <a href="#game-rules">Game rules</a> ·
    <a href="#gem-collection">Gems</a> ·
    <a href="#build">Build</a> ·
    <a href="README.zh-CN.md">简体中文</a>
  </p>
  <p>
    <img alt="Hardware: M5Stack StickS3" src="https://img.shields.io/badge/hardware-M5Stack%20StickS3-EA1D2C">
    <img alt="Display: 135 by 240" src="https://img.shields.io/badge/display-135%C3%97240-111111">
    <img alt="ESP-IDF: 5.5" src="https://img.shields.io/badge/ESP--IDF-5.5-E7352C">
    <img alt="Version: 0.6.3" src="https://img.shields.io/badge/version-0.6.3-F3A712">
    <img alt="Mode: Offline" src="https://img.shields.io/badge/mode-offline-2E8B57">
  </p>
  <br>
  <img src="assets/screenshots/fruit-machine-new-icons-neon-showcase.jpg" alt="Nostalgic Fruit Machine with its new fruit, crown, diamond, 99, SUP, and WOW artwork in a neon arcade" width="900">
</div>

## What's new in v0.6.3

Released on August 1, 2026. Download the firmware and view the complete notes
on the [v0.6.3 release page](https://github.com/oliverxing2025/nostalgic-fruit-machine/releases/tag/v0.6.3).

- **Current-credit gems:** a new gem activates when the live `CREDIT` balance
  first reaches its threshold. Previously spent credit is not accumulated.
- **Permanent collection:** once activated, a gem stays lit after spending,
  resetting, or entering a negative balance.
- **Safe migration:** existing saved data discards the old cumulative-winnings
  counter and restores the permanent gem level from the highest credit balance
  previously reached.
- **One-step bet reduction:** short-press the front blue button to add one to
  the selected symbol; long-press for about 0.9 seconds to subtract one. A
  prepaid unit is refunded automatically.

### Upgrade paths

| Download | Flash offset | Use it when | Saved data |
| --- | ---: | --- | --- |
| `Nostalgic-Fruit-Machine-v0.6.3-app.bin` | `0x20000` | Updating a device whose identity and compatible partition layout have already been verified | Preserves NVS credit, statistics, bets, and gem state |
| `Nostalgic-Fruit-Machine-v0.6.3-full.bin` | `0x0` | Clean standalone installation, or intentionally replacing the existing firmware layout | Resets NVS and all saved game data |

> [!WARNING]
> Verify the physical device identity, partition layout, image, and offset
> before writing. The full image can replace a multi-firmware installation and
> erase saved Fruit Machine data. The application-only image is safe only for
> an already verified compatible layout.

## Overview

Nostalgic Fruit Machine turns the StickS3 into a self-contained miniature
arcade cabinet. The portrait display carries a full 24-cell board, six control
buttons, eight symbol bets, live credit and bonus values, and a bright
fruit-themed center scene—without a phone, account, or network connection.

| | Capability | What it adds |
| --- | --- | --- |
| **01** | Retro running-light board | Accelerates, completes steady laps, decelerates, and stops precisely on a preselected result with synchronized light and sound. |
| **02** | Motion-first control | Navigate the three control rows by tilting left, right, forward, or backward, with re-centering and pickup recovery. |
| **03** | Layered risk and reward | Eight bets, X3 cells, two LUCK modes, small/big risk play, rare jackpots, Big Bang, and free spins. |
| **04** | Long-term collection | Permanently light 30 colored gemstones when current credit first reaches each threshold. |

> Entertainment only. All credits are virtual. The firmware has no recharge,
> withdrawal, cash-out, advertising, account, analytics, or network-gambling
> functionality.

> [!NOTE]
> The promotional images are product renders. Minor visual details may differ
> from the current physical display and firmware revision.

<div align="center">
  <img src="assets/screenshots/fruit-machine-new-icons-product.jpg" alt="Front product view of the Nostalgic Fruit Machine interface on M5Stack StickS3" width="520">
</div>

## Device experience

- 24-cell clockwise running-light board with acceleration, steady laps,
  deceleration, synchronized audio, and illuminated active artwork.
- Eight independent symbol bets from `00` to `99`, plus `ALL+1`, `2X`, `CLR`,
  small, big, and `GO` controls.
- Four-direction selection using the StickS3's BMI270 accelerometer.
- Physical re-centering lock: one tilt triggers one move; return the device to
  its neutral pose before the next move.
- Automatic pickup recovery after the device has been left resting.
- Small/big risk mode with selectable 25%, 50%, 75%, or 100% participation.
- Orange LUCK, blue LUCK, Big Bang, and free spins.
- 30 permanently unlockable gemstones driven by the highest threshold current credit has reached.
- Persistent credit, bets, statistics, sound state, game state, and gem
  progress through NVS.
- Allocation-free runtime UI and a 1,000-round logic self-test on every boot.

## Hardware and software

| Item | Requirement |
| --- | --- |
| Device | M5Stack StickS3 |
| Display | 135 × 240 portrait |
| Motion | BMI270 accelerometer |
| Audio | ES8311 codec and onboard speaker |
| Framework | ESP-IDF 5.5.x |
| UI | LVGL |
| Current firmware | 0.6.3 |

Directional selection uses accelerometer samples only. Gyroscope angle
integration is not used.

## Quick start

1. Short-press the side button to add 5 virtual credits.
2. Tilt the device to move the yellow selection outline.
3. Select a fruit or symbol and short-press the front blue button to add a bet.
4. Select `GO` and short-press the blue button, or double-press the blue
   button from anywhere, to start.
5. After a win, choose the risk percentage and small/big guess, or collect the
   pending win.
6. Long-press the side button for about one second to view the gem collection.

## Complete controls

### Motion selection

Hold the device in a natural portrait grip.

| Device action | Normal betting screen | Pending-win screen |
| --- | --- | --- |
| Tilt left | Move selection left in the current row | Reduce risk: 100 → 75 → 50 → 25% |
| Tilt right | Move selection right in the current row | Increase risk: 25 → 50 → 75 → 100% |
| Tilt forward/up | Move to the nearest control in the row above | Cycle backward through Small, Big, Collect |
| Tilt backward/down | Move to the nearest control in the row below | Cycle forward through Small, Big, Collect |
| Return to neutral | Re-arm motion for the next direction | Re-arm motion for the next direction |

One directional action is generated per tilt. Keep the device centered briefly
before the next action. If the device was left resting and motion appears
inactive, pick it up, hold it naturally for a short moment while it settles,
then tilt and return to center. The firmware detects pickup and recalibrates
the neutral reference automatically.

Motion is intentionally ignored while the running light, bonus chain, Big Bang,
or gem gallery is active.

### Front blue button

| Gesture | Action |
| --- | --- |
| Single press | Activate the selected control |
| Double press | Start `GO`; collect a pending win |
| Long press, about 0.9 s | Decrease the selected symbol bet by one; on `ALL+1`, clear all bets |

### Side button

| Gesture | Action |
| --- | --- |
| Single press | Add 5 virtual credits |
| Four presses, no more than about 0.9 s apart | Reset current credit to 0 |
| Double press | Select the previous control |
| Long press, about 0.9 s | Open or close the 30-gem collection |

While the gem collection is open, other button and motion inputs are ignored
to prevent accidental betting.

## On-screen controls

| Control | Purpose |
| --- | --- |
| Eight symbol tiles | Blue-button short press adds one; long press subtracts one; range `00`–`99` |
| `ALL+1` | Add one bet to all eight symbols |
| `2X` | Double every non-zero symbol bet, capped at `99` |
| `CLR` | Clear every bet |
| `1-6` | Guess small in pending-win mode |
| `8-13` | Guess big in pending-win mode |
| `GO` | Start a spin or collect the pending win |

When a win is pending, the `2X` and `CLR` positions become `-` and `+` for the
risk percentage.

## Game rules

### Credit and betting

- A new installation starts at `0` credits.
- Each side-button single press adds 5 virtual credits.
- Each of the eight categories accepts `00`–`99`.
- Clicking a symbol deducts its configured per-unit cost. A retained bet is
  charged when the next spin starts; prepaid clicks are not charged twice.
- Play may continue below zero; credit is bounded at `-99,999`.
- Gem activation uses current credit only; previously spent credit is not accumulated.

### Board and payouts

The 24 positions begin at the upper-left and proceed clockwise:

```text
00 Orange, 01 Bell, 02 BAR 50x, 03 BAR 100x, 04 Apple, 05 Apple X3,
06 Cyan fruit, 07 Watermelon, 08 Watermelon X3, 09 Blue LUCK,
10 Apple, 11 Orange X3, 12 Orange, 13 Bell, 14 77 X3, 15 77,
16 Apple, 17 Cyan fruit X3, 18 Cyan fruit, 19 Star, 20 Star X3,
21 Orange LUCK, 22 Apple, 23 Bell X3
```

| Symbol | Unit cost | Normal payout |
| --- | ---: | ---: |
| BAR | 10 | 50× or 100×, depending on the cell |
| 77 | 8 | 40× |
| Star | 6 | 30× |
| Watermelon | 4 | 20× |
| Bell | 5 | 20× |
| Cyan fruit | 3 | 15× |
| Orange | 2 | 10× |
| Apple | 1 | 5× |

An `X3` cell pays bet units ×3 and does not also apply the normal symbol
multiplier. The result is chosen with `esp_random()` using per-cell weights
before animation; the state machine then calculates the exact number of steps
required to stop on it after at least two steady laps. The balanced weights
target about 106.36% theoretical return and a 29.27% net-win rate for one unit
on every symbol, before optional small/big gambling.

### Pending win: small, big, or collect

After an ordinary paid win:

1. Choose how much of the pending win participates: 25%, 50%, 75%, or 100%.
2. Choose `1-6` for small or `8-13` for big.
3. A number from 1 to 13 is drawn.
4. A correct guess doubles the participating amount.
5. A wrong guess, or a result of 7, loses the participating amount.
6. The non-participating remainder stays safe.
7. Select `GO` or double-press the blue button to collect.

### Special events

- **Orange LUCK:** awards total bet × `2/3/5/8/10/20/20`.
- **Blue LUCK:** resolves a chain of exactly 5 consecutive cells. A LUCK cell inside
  the chain pays zero and does not recursively start another LUCK event.
- **Big Bang:** when credit reaches total maximum bet × 400, the center scene
  flashes and grants three automatic free spins while retaining the last bet.

## Gem collection

Long-press the side button to open a full-screen 5 × 6 gallery of 30 gems.

<div align="center">
  <img src="assets/screenshots/gem-collection-level-up-showcase.png" alt="The 30-gem collection gallery on an M5Stack StickS3" width="900">
</div>

- Gem 1 is available at 0 credit.
- Gem level `n` unlocks at `50 × (n - 1) × n`.
- Gem 2 unlocks at 100; gem 30 unlocks at 43,500.
- Locked gems are grayscale; unlocked gems use their individual colors.
- A new gem activates only when current credit first reaches its threshold;
  previously spent credit is not accumulated.
- Once activated, a gem remains lit permanently. Spending, resetting, or going
  below zero does not dim it again.
- Side-button top-ups count because they increase the current credit balance.

The complete gem list is documented in
[docs/GEM_COLLECTION.zh-CN.md](docs/GEM_COLLECTION.zh-CN.md).

## Persistence

NVS stores:

- current and highest credit;
- symbol bets and retained bet;
- total rounds, winning rounds, and highest single win;
- pending Big Bang/free-spin state;
- sound settings;
- permanently activated gem level.

Writes are dirty-marked and delayed, then handled by a low-priority persistence
task rather than inside animation timing.

## Project structure

```text
.
├── assets/screenshots/
│   └── nostalgic-fruit-machine-product.png
├── README.md
├── README.zh-CN.md
├── docs/
│   ├── GEM_COLLECTION.zh-CN.md
│   └── portrait-static-preview.html
├── firmware/sticks3/
│   ├── assets/
│   ├── include/
│   ├── src/
│   ├── third_party/bmi270/
│   ├── CMakeLists.txt
│   ├── dependencies.lock
│   ├── partitions.csv
│   └── sdkconfig.defaults
├── output/playwright/
└── tools/
```

`managed_components/`, local `sdkconfig`, build outputs, firmware binaries,
logs, credentials, and editor state are intentionally excluded from Git.

## Build

Install and export ESP-IDF 5.5.x, then:

```sh
cd firmware/sticks3
idf.py build
```

For a standalone Fruit Machine device:

```sh
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Always identify the physical device and inspect the partition table before
writing firmware. A normal full flash can replace an existing multi-app layout.
For an already validated compatible layout, the Fruit Machine application is
currently built for `ota_0` at `0x20000`; never assume this offset for an
unknown device.

Exit the serial monitor with `Ctrl+]`.

## Verification

Every boot runs a 1,000-round allocation-free logic test covering:

- weighted target selection;
- exact stop-step arithmetic;
- minimum two-lap steady running;
- track configuration validity;
- heap stability during the test.

A healthy startup includes:

```text
boot VibeStick Fruit Machine 0.6.3 bet-decrease
1000-round logic self-test PASS heap_delta=0
display portrait 135x240
```

The display canvas and LVGL objects are allocated once and reused. The running
state machine, audio queue, input queue, and NVS task do not create UI objects
per animation frame.

## Asset and dependency notes

The interface, procedural game artwork, and product render are project-owned.
The BMI270 driver is retained under `firmware/sticks3/third_party/bmi270/`
with its upstream license. ESP-IDF managed dependencies are pinned by
`dependencies.lock` and downloaded during configuration; they are not vendored
into this repository.
