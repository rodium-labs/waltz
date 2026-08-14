# Waltz

An MP3 player by **Rodium Labs**, built on an **STM32F401 Black Pill** driving a
**2.25" 76x284 narrow-bar TFT (ST7789P3, 4-wire SPI)**, run on its side as a
284x76 letterbox.

This is the display + UI half of the player. Playback is mocked for now: the
transport, level meter and playlist run off timers so the whole screen can be
exercised without an SD card or a decoder attached.

![Splash](docs/ui-splash.png)

*The splash in four of the themes.*

![UI preview](docs/ui-preview.png)

*Splash, the home screen, the track list, the player.*

![Screens](docs/ui-screens.png)

*Player, settings, and the play-mode chord flipping shuffle then repeat.*

## Wiring

The panel is a 3.3 V part - do not feed it 5 V.

| Module | Black Pill | Function |
| ------ | ---------- | -------- |
| GND    | G          | |
| VCC    | 3V3        | 2.8 - 3.3 V |
| SCL    | PA5        | SPI1_SCK (AF5) |
| SDA    | PA7        | SPI1_MOSI (AF5) |
| RES    | PB1        | GPIO out |
| DC     | PB0        | GPIO out |
| CS     | PA4        | GPIO out |
| BLK    | PB10       | TIM2_CH3, 1 kHz PWM brightness |

Five buttons, each tying its pin to GND - internal pull-ups, no external parts.
All on GPIOA so a scan is one `IDR` read, and clear of the I2S2 (PB12/13/15) and
SPI3 SD (PB3/4/5, PA15) pins the audio side will want later.

| Button | Pin | |
| ------ | --- | - |
| VOL-   | PA0 | some Black Pill revisions already have a KEY button here |
| PREV   | PA1 | |
| PLAY   | PA2 | |
| NEXT   | PA3 | |
| VOL+   | PA6 | SPI1_MISO, free because the panel is write-only |

Onboard LED on PC13 blinks once a second as a heartbeat.

SPI1 runs at 42 MHz (PCLK2 84 MHz / 2), which is the part's ceiling. That is not
about throughput for its own sake - see [Vsync](#vsync) for why the clock has to
beat the panel's own scan. Drop `BaudRatePrescaler` in `MX_SPI1_Init()` to `_4`
if long jumpers start showing noise; that was the known-good setting.

The backlight on this module is **active low**: BLK pulled low lights it. That is
what `ST7789_BLK_ACTIVE_LOW` handles, and `st7789_backlight()` compensates so 0
always means dark.

## Pin map

Everything in use today, and what the audio side has reserved.

| Pin | Use | Peripheral |
| --- | --- | ---------- |
| PA0 | VOL- button | GPIO in, pull-up |
| PA1 | PREV button | GPIO in, pull-up |
| PA2 | PLAY button | GPIO in, pull-up |
| PA3 | NEXT button | GPIO in, pull-up |
| PA4 | panel CS | GPIO out |
| PA5 | panel SCL | SPI1_SCK, AF5 |
| PA6 | VOL+ button | GPIO in, pull-up |
| PA7 | panel SDA | SPI1_MOSI, AF5 |
| PA13 | SWDIO | debug |
| PA14 | SWCLK | debug |
| PB0 | panel DC | GPIO out |
| PB1 | panel RES | GPIO out |
| PB10 | panel BLK | TIM2_CH3, 1 kHz PWM |
| PC13 | onboard LED | GPIO out, heartbeat |
| PH0 / PH1 | 25 MHz crystal | HSE |
| - | supply measurement | ADC1 internal VREFINT, no pin |

Free, and what they are earmarked for:

| Pin | Planned |
| --- | ------- |
| PB12 / PB13 / PB15 | I2S2 - WS / CK / SD to a PCM5102A |
| PB3 / PB4 / PB5 / PA15 | SPI3 - SCK / MISO / MOSI / CS to a microSD |
| PB6 | where VOL+ moves if a battery divider is ever needed on PA6 |
| PA8 / PA9 / PA10 / PB7 / PB8 / PB9 / PB14 / PC14 / PC15 | unclaimed |
| PA11 / PA12 | USB - left alone |
| PB2 | BOOT1, has a pull-down on the board - avoid |

PB11 does not exist on this package, and note that PB3 doubles as SWO: using it
for SPI3 costs trace output but not SWD, which stays on PA13/PA14.

## Build

```bash
cmake --preset Release
cmake --build --preset Release
```

Needs `arm-none-eabi-gcc` from STM32CubeCLT plus `ninja` on `PATH`. Output lands
in `build/Release/` as `.elf`, `.hex` and `.bin`.

**Flash the Release build.** The Debug preset is `-O0`, and on this part that is
not a detail: drawing a screen costs 26.9 ms unoptimised against 8.5 ms at `-Os`,
which is the difference between 24 fps and 92. There is a `Debug` preset for when
a debugger is actually attached.

Current footprint: **37 kB flash of 256 kB, 24 kB RAM of 64 kB** - the renderer
paints in bands rather than keeping a 42 kB framebuffer, which leaves room for a
Helix MP3 decoder later. Two band buffers are 19 kB of that and the gradient row
table another 2.5 kB; both are there for the frame rate, and both are one
constant away from being smaller and slower.

## Flash

With an ST-Link on SWD:

```bash
/opt/ST/STM32CubeCLT_1.20.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI -c port=SWD -w build/Release/waltz.elf -rst
```

Or over USB DFU - hold BOOT0, tap NRST, release BOOT0:

```bash
dfu-util -a 0 -s 0x08000000:leave -D build/Release/waltz.bin
```

## What this panel actually needed

Bringing the module up took a while, so here is the record - all of it lives in
`st7789.h` and `gfx.h` as flags with the reasoning in their comments.

| Symptom | Cause | Flag |
| ------- | ----- | ---- |
| Lit during init, faded out at the splash | BLK is active low | `ST7789_BLK_ACTIVE_LOW 1` |
| Plain white forever, backlight fine | CS held low across the whole init lost command framing; this controller wants CS pulsed per command | `wr_cmd_args()` in `st7789.c` |
| Colours wrong in every combination | The pixel path widened SPI to 16-bit frames per burst and switched back for commands. Nothing else drives ST7789 that way, and it mangled the data. Now plain byte-wide DMA. | `GFX_WIRE_SWAP 1` |
| - | With the data path fixed the panel wants no colour tricks at all | `ST7789_BGR 0`, `ST7789_INVERT 0` |

Two things that did **not** turn out to be the problem, in case they look
tempting: SPI clock speed (1.3 MHz behaved exactly like 21 MHz) and the
CASET/RASET offsets (82/18 portrait, 18/82 landscape - correct from the start).
The clock is at 42 MHz now, but for vsync, not because the panel ever minded.

### Diagnostics

Three bring-up screens are still in the tree, all off by default. Each one
answers a different question and none of them needs test equipment.

* `LCD_GRAM_PROBE` in `main.c` - writes straight to the 240x320 frame memory,
  ignoring the offsets. Floods it red, then splits columns and rows into thirds.
  Answers *is the panel executing commands at all, and where is its window?*
* `LCD_COLOR_SWEEP` in `main.c` - walks all eight colour formats the controller
  can be in (byte order x BGR x inversion), four seconds each, numbered on
  screen. Answers *which format is this panel?* without anyone having to name a
  hue. This is what finally settled it.
* `UI_PANEL_CHECK` in `player_ui.h` - six labelled colour strips held for 15 s.
* `LCD_READ_PROBE` in `main.c` - reads the panel ID and a run of timestamped
  scanline samples into RAM. Answers *will this module answer a read on SDA, and
  how fast does it scan?* - which is what made vsync possible without a TE pin.

The splash itself is a permanent self-test: a 1 px border around all four edges
(any missing edge means the offsets are wrong) plus R/G/B swatches and the
offsets in use.

## Layout

284x76 is a letterbox - 47 characters of the 6x8 font wide but only nine rows
tall - so the screen is three columns rather than a stack:

```
+-------------------------------------------------------+
| shuf rep      NOW PLAYING 3/5       vol      battery  |
+--------+---------------------------+------------------+
|        | title                     | prev play next   |
| cover  | artist                    |                  |
| 64x64  | progress                  | level meter      |
|        | 0:12              4:05    |                  |
|        | 320 kbps                  |                  |
+--------+---------------------------+------------------+
```

Each block is an independent redraw region, so only the ones whose data changed
get repainted - that is what keeps the banded renderer cheap. Row constants and
the palette live in `Core/Inc/Ui/theme.h`.

Portrait (76x284) still works - set `ST7789_ROTATION` to 0 in `st7789.h` - but
the layout above is built for landscape and will not rearrange itself.

## Screens

Boot lands on the **home screen** with nothing playing - four tiles, PREV/NEXT
to pick, PLAY to enter.

| Tile | |
| ---- | - |
| MUSIC | the player |
| TRACKS | track list |
| STATS | lifetime counters and supply readout |
| SETTINGS | settings |

![Menus](docs/ui-menus.png)

*The three list screens: tracks, stats, settings.*

A 12-row **status bar** sits above every screen: playback state, shuffle and
repeat flags, the screen name, volume, battery. Keeping it global is why the
player below it fits in 64 rows.

Every list wraps - past the last row is the first one again, in the menus and
across the home tiles. Long lists get a position rail on the right, since
wrapping otherwise leaves no sense of where you are.

A sixth screen has no tile: the **message screen**, which any button dismisses.
It is where the no-card and no-tracks states will land.

## Feedback

![Overlays](docs/ui-overlays.png)

*The volume card, a settings row being edited, the message screen, and a flat
battery.*

Three things the screens themselves could not say:

**Volume** used to be a six pixel number in the corner of the status bar, which
is not where anyone is looking when they press the button. Now any volume press
raises a card over the content for a second. It takes the screen back when it
expires by repainting whole - nothing recorded what it covered, and with no
framebuffer there is nothing to restore from.

**Position in a list.** Five rows are visible and the lists wrap, so with a card
full of tracks there would be no sense of place at all. A rail on the right
carries a proportional thumb. The lane is reserved whether or not the list is
long enough to need one, so rows do not shift when it appears.

**A flat battery.** Under 15% the case turns red as well as the level - a two
pixel sliver of red inside a grey outline is easy to walk past. The one-shot
notice behind it is gated on `Power_HasBatterySense()`, which is false today: the
rail is regulated, so it will not sag with the cell, and warning off it would be
crying wolf. It starts working the day a divider goes in.

The message screen is for the states that leave nothing to play - no card, no
readable tracks, a decoder that gave up. Nothing reaches it while the playlist
is mocked, but `Ui_ShowMessage()` is there and the empty-playlist path already
routes to it.

## Frame rate and vsync

The panel had a visible tear that walked across the screen, and chasing it meant
measuring rather than guessing. There is no console on this board, so the numbers
live in RAM and come out over SWD:

```bash
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 0x20004b6c 16
```

`UI_FRAME_TIMING` in `player_ui.h` puts the last content repaint, the worst one,
the count, and how much of it was drawing rather than transfer, at
`ui_frame_us` and friends. One full-screen repaint, measured four ways:

| | Draw | Transfer | Frame | fps |
| - | ---- | -------- | ----- | --- |
| `-O0`, serial, 21 MHz | 26.9 ms | 14.0 ms | 41.0 ms | 24 |
| `-Os` | 8.3 ms | 14.0 ms | 22.3 ms | 45 |
| + overlapped bands | 8.5 ms | 14.0 ms | 15.6 ms | 64 |
| + 42 MHz | 8.5 ms | 7.0 ms | 10.9 ms | 92 |
| + bands turned to face the scan | 12.1 ms | 7.0 ms | 13.2 ms | 76 |
| + gradient rows remembered | 9.0 ms | 7.0 ms | 10.4 ms | 96 |

The first line is the embarrassing one: every flash until then had been a Debug
build. `cmake --preset Release` is `-Os` and it alone took the frame from 41 ms
to 22 ms.

The second step is that `gfx_flush()` no longer takes turns. It keeps two band
buffers and draws into one while the DMA is still pushing the other, so the
drawing hides underneath the transfer instead of adding to it.

The fifth line goes backwards on purpose - turning the bands to run down the
screen is what makes vsync possible at all, and it cost 3 ms because every band
redraws the whole screen with most of it clipped away. Two things took that back:
primitives that miss the band now return before walking their geometry, and
gradient rows are worked out once and read back by the bands after. Panels that
share a colour pair share the table, which on the home screen is four of the five
tiles.

### Vsync

Tear-free needs more than speed - it needs to know where the panel is looking.
That normally comes from a TE pin, and this module does not break one out.

It does not have to. The controller will answer `GSCAN` (0x45) with the line it
is currently displaying, and although there is no SDO pad and PA6 is a button,
SPI can be put in half duplex so the controller replies on SDA itself. That is
`rd_cmd()` in `st7789.c`, and `st7789_read_probe()` is what settled that this
module plays along - it returns an ID of 81 81 B3 and a scan counter that climbs
and wraps:

```
0 -> 23 -> 49 -> 90 -> 131 -> 173 -> 214 -> 255 -> 296 -> [1] -> 35 -> ...
```

Fitting a rate to that gives **48.8 us a line, and a frame every 16.63 ms - the
panel free-runs at 60.1 Hz.**

The geometry matters here. In landscape it is the 284 px dimension that lands on
the controller's 320 gate lines, so the panel scans our screen *sideways* and
the tear is a vertical line travelling across. Our strip covers scan lines 18 to
301.

So the write is a race along the same axis. At 21 MHz a full content write took
49.3 us per column against the scan's 48.8 us - fractionally slower, so the scan
overtook it every single frame no matter where it started. That is why the clock
had to go up: at 42 MHz a column costs 38.2 us, and starting during the porch
puts every column down well before the scan arrives. The write finishes with the
scan only a third of the way across.

`gfx_sync_next()` arms that, and the first `gfx_flush()` of the frame consumes
it - a screen built from several regions is still one frame and waits once, not
once per region. Nothing waits on a tick that draws nothing, so an idle screen
keeps polling buttons at full rate.

Two deadlines have to be met, and both are measured rather than assumed. The
first band has to be down before the scan re-enters the strip, and the whole
frame has to be down before the scan crosses it. Over 663 frames of being driven
around by hand:

| | Measured | Budget | Margin |
| - | -------- | ------ | ------ |
| First band | 2.71 ms | 3.17 ms porch | +461 us |
| Steady frame | 10.38 ms | 13.65 ms crossing | +3.27 ms |
| Worst frame - a transition | 13.56 ms | 13.65 ms crossing | +88 us |

The worst case is a screen transition, which has two whole screens in flight.
88 us is under two scan lines, so it is inside but not comfortably; if it ever
needs real headroom, lengthening the line period through `FRCTRL2` buys about
600 us at the cost of dropping the refresh to around 57 Hz.

The line period is calibrated at boot rather than hardcoded, because it is what
the wait counts in. It takes the median of eight rising samples: a single pair
is only right if the counter did not wrap in between, and a wrap looks exactly
like a slow panel - which is how the first version came back with a line period
less than half the real one.

## Surfaces

Panels are frosted rather than flat: the background showing through, tinted, with
a rim picking out the edge. The backdrop is a gradient that takes its colour from
the current cover art, so the whole screen shifts with the track.

Doing that properly normally means reading back what is behind a panel and
blurring it, which needs a framebuffer this board cannot afford. The way around
it is to keep the backdrop as a *formula* instead of pixels - it is linear in y,
so `backdrop_row(y)` returns what is behind any row without anything having been
drawn yet. A translucent layer over a linear backdrop is still linear, so a panel
is one gradient fill of two computed endpoints. No read-back, no second buffer.

```c
static uint16_t backdrop_row(int16_t y);                     /* what is behind */
static void glass_panel(int16_t x, int16_t y, int16_t w, int16_t h,
                        int16_t r, uint8_t alpha, uint16_t tint);
```

Two details carry it. The rim is what sells the effect - a flat translucent
rectangle reads as a faded box, an edge catching light reads as a pane - and only
panes at least 16 px deep get the brighter specular line along the top, because
on a 12 px menu row it just crowds the text above.

The other is dithering. Five and six bits per channel across 76 rows is not
enough for a smooth ramp, and the first version banded visibly. `gfx_vgrad()` and
`gfx_rrect_grad()` now dither with an ordered 4x4 Bayer matrix, which trades the
bands for a stipple that disappears at the panel's pixel pitch.

One primitive came out of looking closely at the cover. `gfx_ring()` with the
radii one apart is a true annulus, and near the top and bottom a scanline
crosses it almost horizontally, so it flares into a wide cap - the record's
grooves had a visible seam at every cardinal point. `gfx_circle()` strokes the
curve instead and stays one pixel the whole way round.

## Motion

| | |
|---|---|
| ![Push](docs/anim-F2push.gif) | ![Pop](docs/anim-F4pop.gif) |
| Pushing into a screen | Popping back out |
| ![Focus](docs/anim-F1focus.gif) | ![Menu](docs/anim-F5menu.gif) |
| The home focus ring moving | The menu highlight walking |
| ![Spin](docs/anim-F6spin.gif) | |
| The record turning while it plays | |

*Captured from the simulator every 20 ms, so this is the real timing.*

Transitions carry the hierarchy: going into something pushes it in from the
right, coming back slides it away again. Selection does not jump either - the
home focus ring and the menu highlight slide to their new place.

Animation is time-based rather than frame-based, so a slow frame shortens the
animation instead of stretching it. Ease-out cubic over 240 ms for screens,
160 ms for selection.

The cover turns while a track plays and parks the moment it is paused, so a
still screen stays still. One sweep of light across the grooves carries it,
stepped from a 32 byte cosine table - a revolution every three seconds, which is
slower than a real platter because the job is to say *playing* at a glance.

None of it needs a framebuffer. `gfx_translate()` shifts the coordinate origin
the primitives draw against, so a transition paints the outgoing screen at one
offset and the incoming one at another inside a single banded flush, and neither
screen's paint function knows it is being moved.

The one thing that had to change to make that safe was clipping. It used to fall
out of the band for free, which held while every paint function owned exactly
the region it was flushed with - and broke the moment the marquee was reused
inside a whole-screen repaint, where it happily drew its text across the album
art. `gfx_clip()` makes it explicit.

## Controls

PLAY is the only button with a long press, and it always means the same two
things: tap to act, hold to go back.

**Player**

| | |
| - | - |
| PLAY short | play / pause |
| PLAY held | back to home |
| PREV | previous track, or restart the current one past 3 s |
| NEXT | next track |
| PREV / NEXT held | seek in 5 s steps, repeating |
| VOL-/VOL+ | one step, auto-repeating when held |

**Track list and stats**

| | |
| - | - |
| PREV / NEXT | move the selection, auto-repeating |
| PLAY short | play the highlighted track |
| PLAY held | back to home |
| VOL-/VOL+ | volume, on the card |

**Settings** - PLAY steps into a row rather than cycling it in place. THEME has
ten schemes, and overshooting one used to mean nine more presses to come back
around.

| | |
| - | - |
| PREV / NEXT | move the selection, or step the value when inside a row |
| PLAY short | enter or leave the row; on/off rows just flip |
| PLAY held | leave the row, or leave settings - which is the save point |
| VOL-/VOL+ | volume, on the card |

The row being edited says so: its plate brightens and chevrons appear either
side of the value.

**Anywhere**

| | |
| - | - |
| VOL- + VOL+ together | cycle the play mode: off, shuffle, repeat, both |
| any button, screen off | wakes only - it does not also act |

That chord exists because every short and long press across the five buttons was
already spoken for, and digging into settings to flip shuffle is too far to
reach. Cycling all four states with one gesture is what makes a single chord
enough. The volume pair is the safe one to overload: the first press of the pair
still lands a volume step, and a stray 2 % is harmless where a stray track skip
would not be. The status bar flags are the feedback.

Auto-repeat matters more than it sounds: without it, walking the volume from
20 % to 80 % is thirty presses. First repeat after 400 ms, then every 100 ms,
speeding to 50 ms after a second.

`input.c` polls the port every 5 ms from the main loop and debounces on four
agreeing samples - no EXTI, no per-button timer. A press lasts 50-200 ms and the
worst-case blit is 16 ms, so nothing is missed.

## Settings

Theme, backlight brightness, screen-off delay, fade speed, shuffle, repeat. PLAY
changes the highlighted row.

**Screen off** blanks the backlight after 15 s / 30 s / 1 min / 5 min of no
button, or never. It *fades* rather than switching - a panel that snaps to black
reads as a fault. **Fade** sets how fast: instant, fast, normal or slow (10 ms to
a full second). The ramp is stepped from the main loop rather than slept through,
so a button press can interrupt a fade half way down. Playback carries on
regardless; it is a music player. The press that wakes the screen is swallowed,
so reaching for the volume does not skip a track.

**Stored in flash.** Leaving the settings screen appends a 16-byte record to
sector 5 (the last 128 kB, well clear of the 40 kB firmware). Flash bits only go
1 -> 0 without an erase and erasing that sector stalls the CPU for about a
second, so instead of rewriting one slot each save appends and the loader takes
the last valid record - 8192 saves before an erase is needed. See
`Core/Src/settings.c`.

## Themes

![Themes](docs/ui-themes-a.png)
![Themes](docs/ui-themes-b.png)

*The player screen in all ten schemes.*

Ten of them in `Core/Src/Ui/theme.c`: `NIGHT`, `AMBER`, `MINT`, `PAPER` (light),
`TERMINAL`, `OCEAN`, `ROSE`, `SLATE`, `SUNSET` and `MONO`. Switch from the
settings page; the change applies to every screen immediately.

The splash follows the stored theme too, which is why `Settings_Load()` runs
before `Ui_Splash()` in `main()` - load it later and the splash comes up in the
defaults and then the first real screen snaps to the stored palette. The splash
fade also uses the stored brightness and the FADE step, so it comes up at
whatever speed the rest of the UI moves at.

The palette used to be a set of `COL_*` macros. It is now a `ui_theme_t` table
with a pointer to the active entry, and the `COL_*` names were kept as macros
that dereference it - so the drawing code did not change at all when themes
landed. Per-track cover gradients stay compile-time constants in `player.c`,
because they belong to the track rather than the theme.

**Nothing may cache a `COL_*` value.** They are runtime lookups now, so a copy
taken at init goes stale the moment the theme changes - which is exactly how the
marquee ended up painting the old background behind the title and artist, a
visible box that did not match the rest of the screen. Resolve colours inside the
paint function.

`PAPER` is in there deliberately: a light scheme is what catches any place the UI
quietly assumed a dark background. It has already earned its keep twice - it
caught the level meter's groove being *dimmed towards black* (subtle on a dark
theme, solid bars on a light one; it now mixes from the background towards the
card colour instead), and it made the stale-marquee bug obvious.

`MONO` is greyscale except for red, because a low-battery warning that reads as
just another shade of grey would be a regression.

Nothing about the theme is baked in at build time - the whole table costs 10 x 26
bytes of flash.

## Stats

| Row | Where it comes from |
| --- | ------------------- |
| LISTENING | real seconds of playback, counted from `HAL_GetTick` deltas rather than transport ticks - so it stays honest while `DEMO_TIME_SCALE` runs the transport fast |
| TRACKS PLAYED | every call through `begin_track()` |
| POWER ONS | incremented by `Settings_Load()` |
| SUPPLY | measured, see below |
| BATTERY | `NO SENSE` - no cell, and no pin to sense one |
| CHARGE CYCLES | `-`, same reason |

Counters live in the same flash record as the settings and are committed every
five minutes of playback. There is no power-down warning to flush on, so yanking
the cable can cost the last few minutes - which beats a flash write every second.

### What can and cannot be measured

`SUPPLY` is real. With no external parts the ADC can still read its own 1.21 V
reference, and VDDA falls out of that:

```
VDDA = 4095 * 1210 mV / adc_reading
```

The F4 ships no factory calibration for VREFINT (unlike L4/G4), so the datasheet
typical is all there is - about +-2.5 %, so +-80 mV. That makes it a **brown-out
gauge, not a fuel gauge**, which is genuinely useful when running off an
ST-Link's weak 3.3 V rail. The status bar battery icon shows this rail mapped
2.90 V - 3.30 V onto 0-100 %.

Battery percentage and charge cycles need a cell and a divider, and **there is no
ADC pin left**: on this package ADC1 only reaches PA0-PA7, PB0 and PB1, and every
one of those is a button or a display line. To fix it, move VOL+ off PA6 to PB6 -
then PA6 is ADC1_IN6 and free for the divider. `Power_HasBatterySense()` is the
switch that lights those two rows up.

### Memory cost

The whole stats feature is noise on this part: about 100 bytes of RAM (12 for the
counters, ~90 for the ADC handle) and 6 kB of flash, most of it HAL ADC. Current
totals are **44 kB flash of 256 kB and 11.5 kB RAM of 64 kB**.

The one thing that will actually squeeze RAM is the MP3 decoder: roughly 30 kB
for Helix plus 9 kB of PCM double-buffer. The lever for that is already in place -
`GFX_BAND_H` can drop from 16 to 4, taking the band buffer from 9 kB to 2.3 kB
with no change to any drawing code.

## Source layout

```
Core/Src/main.c            clocks, SPI1, TIM2, main loop
Core/Src/player.c          mocked transport + level meter (playlist table here)
Core/Src/input.c           five buttons -> debounced semantic events
Core/Src/settings.c        settings + lifetime counters in the last flash sector
Core/Src/power.c           supply rail via the ADC's internal reference
Core/Src/Ui/player_ui.c    all five screens, one paint function per block
Core/Src/Ui/theme.c        the ten colour schemes
Core/Inc/Ui/theme.h        palette and block geometry
Libs/st7789/               panel driver: init, windowing, DMA blits
Libs/gfx/                  banded RGB565 renderer + 1bpp fonts
Assets/Icons/              hand-drawn 1bpp glyphs (note, headphones, speaker)
Tools/uisim/               renders the UI on the host - see below
```

Fonts are the tables from [afiskon/stm32-ssd1306](https://github.com/afiskon/stm32-ssd1306)
(MIT), same as the SSD1306 build of this player, so text renders identically on
both. `Font_Roboto16` is Roboto Thin (Apache 2.0).

## Iterating on the UI without hardware

`gfx.c`, `player_ui.c` and `player.c` have no HAL dependencies beyond
`HAL_GetTick`/`HAL_Delay`, so they compile for the host with a stubbed panel:

```bash
./Tools/uisim/run.sh
```

That rewrites every image in `docs/` from the current sources. The stub also
aborts on any blit that falls outside the panel, so it catches layout overruns
that would silently corrupt the display.

Three scripts turn the raw frames into something reviewable, all standard
library only:

| | |
|---|---|
| `sheet.py` | frames -> one scaled contact sheet |
| `zoom.py` | crops blown up 8-10x, for judging glyphs and icons |
| `gif.py` | a frame run -> a looping GIF, median-cut palette and LZW |

The scripted button presses drive the real event handler, so the scenes go
wherever the firmware would. Home navigation goes through `home_pick()`, which
counts presses against the tile the UI is on - the strip wraps, and scenes that
assumed a tile index silently filmed the wrong screen for a while.

## Not done yet

* Audio. The F401 has no DAC, so this wants an I2S codec (PCM5102A, MAX98357A)
  on SPI2/I2S2, or a VS1053B doing the decoding in hardware.
* SD card and a real playlist.
* An up-next card. It was in the portrait layout; 76 rows have no room for it
  alongside the transport row.
* Real playback timing. `DEMO_TIME_SCALE` runs the transport 4x while there is
  nothing to actually decode; set it to 1 for wall-clock.

## License

MIT - see [LICENSE](LICENSE).

Third-party code keeps its own terms:

* `Drivers/` - ST's CMSIS headers and the STM32F4xx HAL, BSD-3-Clause, with
  ST's own `LICENSE.txt` files kept alongside them.
* `Libs/gfx/Src/gfx_fonts.c` - bitmap tables from
  [afiskon/stm32-ssd1306](https://github.com/afiskon/stm32-ssd1306), MIT.
* `Font_Roboto16` in the same file - Roboto Thin by Google, Apache License 2.0.
