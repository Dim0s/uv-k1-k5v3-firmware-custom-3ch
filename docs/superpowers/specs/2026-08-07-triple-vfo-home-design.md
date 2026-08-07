# Triple-VFO Home Screen Design

Date: 2026-08-07  
Status: Approved for planning  
Approach: Full rewrite of home UI + true 3-VFO radio path (Approach 1)

## Goals

Replace the existing dual-VFO home screen with a three-channel home UI, extend the radio to three real VFOs with round-robin receive polling, and map the three physical PTT-capable keys to the three channels.

Non-goals for this change:

- EEPROM migration / backward compatibility with older dual-VFO saves (first flash only)
- Keeping Dual Watch / Cross Band as user-facing modes
- Keeping configurable Side1/Side2 action menus

## References

- Dondji dual-watch bottom S-meter face and 3×3 box fill: `Dondji/App/ui/main.c` (`DualVfoDrawBottomSMeterAndBattery`, `DualVfoDrawSmeterXbm`, `DualVfoDrawSmeterBoxesUv`) and `dualvfo_smeter_xbm.h`
- Small text font for home params and S reading: `u8g2_font_5_tr` (same font Dondji uses for `S1`–`S9` text)
- Channel name / frequency fonts: existing dual-watch home fonts (`UI_PrintStringSmallNormal` / `UI_PrintStringSmallBold` style), not `gFont3x5`

---

## §1 Home layout (128×64)

Remove the classic status bar path on the home screen, TX mic bar, and audio scope / ripple.

```
y≈0–11   [Sn] [Dondji scale + boxes] … [RX|TX] … [dBm] [battery ± text]
y≈12–28  Channel 1 row
y≈29–45  Channel 2 row
y≈46–62  Channel 3 row
```

### Top meter row

- Left: `Sn` text (`u8g2_font_5_tr`) + Dondji-style scale (9 vertical ticks XBM + up to nine 3×3 filled boxes by S-level)
- Center: `RX` or `TX`
- Right: dBm value + small battery icon; beside battery show voltage / percent / nothing per existing BatTxt menu
- On TX: do not draw `Sn` or dBm; reuse the scale area as a TX audio-level bar (random/simple level OK); center shows `TX`; battery still drawn

### Per-channel row

Left block (`u8g2_font_5_tr`, 2px gap between items, left-aligned):

- Line 1: modulation · power letter (`L1`…`H`) · SQL value (number only, e.g. `2`)
- Line 2: offset direction `+` / `-` only if offset enabled; TX CTCSS/DCS only if configured; omit when absent

Right block (existing home name/freq small fonts, right-aligned):

- Top: channel name (fallback `CH-xxxx` if empty)
- Bottom: frequency; while that channel is transmitting, show TX frequency

Whole-row reverse video (white↔black):

- Triggered by incoming signal on that channel, or by long-press `2` / `F+2` selection
- Single highlight index: later event replaces earlier; highlight remains until the next change

---

## §2 Three-VFO data and polling

### Data model

- Expand `VfoInfo`, `ScreenChannel`, `MrChannel`, `FreqChannel`, and related per-VFO arrays from `[2]` to `[3]`
- `TX_VFO` / `RX_VFO` ∈ `{0,1,2}`
- Shared highlight index `gHighlightVfo` for reverse-video row and “current edit channel”
- EEPROM stores three channel pointers and three VFO blocks; factory defaults are fine (no migrate-from-dual path)

### Receive polling (replaces Dual Watch)

- Idle: round-robin tune `0 → 1 → 2 → 0` using the existing dual-watch idle/timer gating pattern, extended to three
- On signal: stop on that VFO, set `gHighlightVfo`, reverse that row until next signal or manual select
- Dual Watch and Cross Band runtime paths removed/disabled; home always behaves as triple watch

### Transmit

- TX parameters come from the VFO bound to the pressed PTT key (§3)
- During TX, UI shows that channel’s TX frequency; top row switches to TX audio-bar mode (§1)

### Editing

- Long-press `2` and `F+2`: cycle `gHighlightVfo` `0 → 1 → 2`, set current edit/main channel to that VFO (replaces A/B-only `COMMON_SwitchVFOs`)
- Incoming-signal highlight uses the same reverse-video slot (overwrites manual highlight)

---

## §3 Keys, triple PTT, menu hiding

### Triple PTT (press = start TX, release = stop TX)

| Key | Transmit channel |
|-----|------------------|
| Main PTT | Channel 1 (VFO 0) |
| Side1 | Channel 2 (VFO 1) |
| Side2 | Channel 3 (VFO 2) |

- No short/long functional split on Side1/Side2; press transmits the mapped channel
- On TX start, select that VFO as current TX/RX and refresh highlight
- Side2 during TX no longer used for 1750 Hz tone (conflicts with triple PTT)

### Selection shortcut

- Long-press `2` and `F+2`: cycle highlight among three channels (see §2)

### Menus hidden (not selectable)

- Dual Watch
- Cross Band
- Side1/Side2 short and long action configuration items

### Menus kept

- Battery text mode (NONE / VOLTAGE / PERCENT) for top-row battery annotation
- Per-channel parameter menus (modulation, power, CTCSS/DCS, SQL, etc.) act on the currently selected VFO

---

## §4 Implementation touchpoints

| Area | Files | Work |
|------|-------|------|
| Home UI | `App/ui/main.c` (+ optional `triple_vfo_*.c/h`) | New home draw path; remove audio bar/scope and old dual-VFO home layout |
| S-meter assets | Port from Dondji: smeter XBM + `u8g2_font_5_tr` draw helper | Scale + small text |
| Status bar | `App/ui/status.c` | Do not draw classic status bar on home; battery moves to top meter row |
| Radio | `App/radio.c/h`, `App/app/app.c` | 3-way poll; TX/RX VFO ∈ {0,1,2} |
| Settings/EEPROM | `App/settings.c/h` | Arrays `[3]`; third VFO load/save + defaults (no migration) |
| Keys | `App/app/generic.c`, `action.c`, `app/main.c`, `common.c` | Triple PTT; long-2 / F+2 cycle three |
| Menu | `App/ui/menu.c` | Hide DW / XB / side-key action items |

### Explicit removals / disable on home path

- `UI_DisplayAudioBar`, `UI_DisplayAudioScope` home calls
- Old center-line `DisplayRSSIBar` layout (replaced by top meter row)
- Dual Watch / Cross Band runtime branches
- Configurable Side1/Side2 `ACTION_OPT_*` entry points

### Risks

- 128×64 vertical budget is tight; long channel names must truncate
- Triple poll timing must avoid retuning too aggressively and missing opens
- No EEPROM compatibility: first flash / settings reset is expected

---

## Decisions log

| Topic | Choice |
|-------|--------|
| S-meter readout | Hybrid B: Dondji scale + boxes; left `Sn`; right dBm |
| Small font | `u8g2_font_5_tr` |
| Name/freq font | Existing dual-watch home small fonts |
| Third channel | True `VfoInfo[3]` |
| Side keys | Press = PTT for CH2/CH3; hide side-key menus |
| Highlight | Single reverse-video row; signal or long-2/F+2; later wins |
| DW/XB | Hidden; always triple poll |
| Left params | 5 items: mod/power/SQL then ±/tone; omit when unset |
| Status bar | Removed on home; center RX/TX only; TX replaces S/dBm with audio bar |
| EEPROM | No backward compatibility |

## Approval

Sections §1–§4 approved in design discussion (2026-08-07). Ready for implementation planning.
