# 怀旧水果机 / Nostalgic Fruit Machine

[中文说明](README.zh-CN.md) · English

An offline retro running-light fruit game built specifically for the
**M5Stack StickS3**. It combines a 24-cell light chase, eight symbol bets,
small/big risk play, animated sound and lighting, four-direction motion
selection, and a 30-gem collection system on a 135 × 240 display.

> Entertainment only. All credits are virtual. The firmware has no recharge,
> withdrawal, cash-out, advertising, account, analytics, or network-gambling
> functionality.

## Highlights

- 24-cell clockwise running-light board with acceleration, steady laps,
  deceleration, synchronized audio, and illuminated active artwork.
- Eight independent symbol bets from `00` to `99`, plus `ALL+1`, `2X`, `CLR`,
  small, big, and `GO` controls.
- Four-direction selection using the StickS3's BMI270 accelerometer.
- Physical re-centering lock: one tilt triggers one move; return the device to
  its neutral pose before the next move.
- Automatic pickup recovery after the device has been left resting.
- Small/big risk mode with selectable 25%, 50%, 75%, or 100% participation.
- Orange LUCK, blue LUCK, rare jackpot events, Big Bang, and free spins.
- 30 unlockable gemstones driven only by cumulative collected winnings.
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
| Current firmware | 0.6.0 |

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
| Long press, about 0.9 s | Clear the selected symbol bet; on `ALL+1`, clear all bets |

### Side button

| Gesture | Action |
| --- | --- |
| Single press | Add 5 virtual credits |
| Double press | Select the previous control |
| Long press, about 0.9 s | Open or close the 30-gem collection |

While the gem collection is open, other button and motion inputs are ignored
to prevent accidental betting.

## On-screen controls

| Control | Purpose |
| --- | --- |
| Eight symbol tiles | Add one bet to the selected symbol; maximum `99` each |
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
- `GO` deducts the total bet.
- Play may continue below zero; credit is bounded at `-99,999`.
- Top-ups never count toward gem progress.

### Board and payouts

The 24 positions begin at the upper-left and proceed clockwise:

```text
00 Orange, 01 Bell, 02 BAR 6x, 03 BAR 12x, 04 Apple, 05 Apple X3,
06 Cyan fruit, 07 Watermelon, 08 Watermelon X3, 09 Blue LUCK,
10 Apple, 11 Orange X3, 12 Orange, 13 Bell, 14 77 X3, 15 77,
16 Apple, 17 Cyan fruit X3, 18 Cyan fruit, 19 Star, 20 Star X3,
21 Orange LUCK, 22 Apple, 23 Bell X3
```

| Symbol | Base payout |
| --- | ---: |
| BAR | 6× or 12×, depending on the cell |
| 77 | 4× |
| Star | 4× |
| Watermelon | 4× |
| Bell | 3× |
| Cyan fruit | 3× |
| Orange | 3× |
| Apple | 2× |

An `X3` cell triples the applicable payout. The current configuration gives
all 24 cells equal selection weight. The result is chosen with `esp_random()`
before animation; the state machine then calculates the exact number of steps
required to stop on it after at least two steady laps.

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

- **Orange LUCK:** awards total bet × `2/2/2/3/3/5/8/10`.
- **Blue LUCK:** resolves a chain of 2–3 consecutive cells. A LUCK cell inside
  the chain pays zero and does not recursively start another LUCK event.
- **Rare jackpot:** includes All Lights, Big Three, Small Three, Eight
  Immortals, and a random 50–200× total-bet reward.
- **Big Bang:** when credit reaches total maximum bet × 400, the center scene
  flashes and grants three automatic free spins while retaining the last bet.

## Gem collection

Long-press the side button to open a full-screen 5 × 6 gallery of 30 gems.

- Gem 1 is available at 0 cumulative collected winnings.
- Gem level `n` unlocks at `50 × (n - 1) × n`.
- Gem 2 unlocks at 100; gem 30 unlocks at 43,500.
- Locked gems are grayscale; unlocked gems use their individual colors.
- Only winnings actually collected into credit advance the gallery.
- Side-button top-ups do not advance the gallery.

The complete gem list is documented in
[docs/GEM_COLLECTION.zh-CN.md](docs/GEM_COLLECTION.zh-CN.md).

## Persistence

NVS stores:

- current and highest credit;
- symbol bets and retained bet;
- total rounds, winning rounds, and highest single win;
- pending Big Bang/free-spin state;
- sound settings;
- cumulative collected winnings and gem level.

Writes are dirty-marked and delayed, then handled by a low-priority persistence
task rather than inside animation timing.

## Project structure

```text
.
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
boot VibeStick Fruit Machine 0.6.0 gem-gallery
1000-round logic self-test PASS heap_delta=0
display portrait 135x240
```

The display canvas and LVGL objects are allocated once and reused. The running
state machine, audio queue, input queue, and NVS task do not create UI objects
per animation frame.

## Asset and dependency notes

The interface and procedural game artwork are project-owned. The BMI270 driver
is retained under `firmware/sticks3/third_party/bmi270/` with its upstream
license. ESP-IDF managed dependencies are pinned by `dependencies.lock` and
downloaded during configuration; they are not vendored into this repository.
