# Triple-VFO Home Implementation Plan

> **For agentic workers:** Execute task-by-task. Steps use checkbox syntax.

**Goal:** Replace dual-VFO home with 3-channel UI, true 3-VFO polling, and triple PTT.

**Architecture:** Expand EEPROM/VFO arrays to 3; replace Dual Watch with round-robin RX; rewrite `UI_DisplayMain` as top meter + 3 rows; Side1/Side2 become PTT for VFO1/2.

**Tech Stack:** C firmware (F4HWN/Quansheng style), ST7565 128×64, BK4819, u8g2 `font_5_tr` + Dondji smeter XBM.

**Spec:** `docs/superpowers/specs/2026-08-07-triple-vfo-home-design.md`

---

### Task 1: Expand VFO arrays to 3 + highlight index

**Files:** `App/settings.h`, `App/settings.c`, `App/radio.c`, `App/radio.h`, callers using `[2]` / `!vfo` / `% 2`

- [ ] Change per-VFO arrays to `[3]`; add `gHighlightVfo`
- [ ] EEPROM load/save third channel; factory defaults for VFO2
- [ ] Fix binary toggles `!gEeprom.TX_VFO` / `RX_VFO ^= 1` to modulo-3 where needed
- [ ] Build-fix compile errors from size-2 assumptions

### Task 2: Triple receive polling

**Files:** `App/app/app.c` (`DualwatchAlternate` and schedulers)

- [ ] Always poll 0→1→2 when idle (DW/XB treated as always-on triple)
- [ ] On RX stop on that VFO; set `gHighlightVfo`
- [ ] Hide/disable DW and XB menu effects

### Task 3: Triple PTT + channel cycle keys

**Files:** `App/app/generic.c`, `App/app/action.c`, `App/app/main.c`, `App/app/common.c`

- [ ] Main PTT → VFO0; Side1 → VFO1; Side2 → VFO2
- [ ] Hide side-key action menus
- [ ] Long-2 / F+2 cycle `gHighlightVfo` 0→1→2

### Task 4: Port S-meter assets + u8g2 small font

**Files:** Port from Dondji into `App/ui/`

- [ ] `triple_vfo_smeter_xbm.h` (or reuse name)
- [ ] `u8g2_font_5_tr` draw helper for home small text

### Task 5: Rewrite home UI

**Files:** `App/ui/main.c`, `App/ui/status.c`

- [ ] Top row: Sn + scale + RX/TX + dBm + battery; TX audio bar mode
- [ ] Three channel rows with left params / right name+freq / reverse video
- [ ] Remove audio bar, audio scope, classic status bar on home

### Task 6: Hide menus

**Files:** `App/ui/menu.c`

- [ ] Hide Dual Watch, Cross Band, Side1/2 short/long action items

### Task 7: Build verify

- [ ] Configure + build firmware preset; fix errors
---
