# Sony V1 protocol in Aural — what is implemented, and on what evidence

This file tracks the Sony Tandem V1 (MDR) messages the engine sends and decodes, and how much
we actually know about each one. It is the in-repository companion to the vault note
*Sony Sound Connect 13.3.0 – Protocol Findings* and the reference implementation in
`protocol-reference/xm4_remaining.py`.

## Evidence levels

| Tag | Meaning |
|---|---|
| **HW** | Exercised against a real WH-1000XM4 (see `Client/qt/UI_REVIEW.md` and `VERIFICATION.md`). |
| **APK** | Recovered statically from Sound Connect 13.3.0 serializers, decoders and callers. No capture. |
| **GB** | Layout taken from Gadgetbridge's V1 implementation (the source of several existing commands). |
| **Test** | Covered only by synthetic tests against `FakeBluetoothConnector`. |

Nothing tagged APK or GB has been confirmed on hardware. A passing test proves the C++ matches
the recovered evidence; it does not prove a headset accepts the bytes.

## Framing and dispatch

| Item | Evidence | Notes |
|---|---|---|
| `3E escape(type seq len32 payload checksum) 3C` | HW, APK | Unchanged transport. |
| Declared length must match the frame | APK, Test | `unpackBtMessage` now reads the full big-endian length and rejects frames whose length disagrees with the bytes present. It used to trust only the low byte, which read past the end of a short frame and truncated payloads over 255 bytes. |
| Tables | APK, Test | `0C` = table 1 (DATA_MDR), `0E` = table 2 (DATA_MDR_NO2). Replies dispatch on table first, then command, then selector. Each decoder re-checks the table, so a table-1 `37` (UPDT_RET_PARAM) can never reach the table-2 paired-device decoder. |
| Malformed input | Test | Every decoder checks command, selector, exact length and enumerated values before reading. A frame that fails leaves state untouched. |

## Capability discovery

| Message | Evidence | Notes |
|---|---|---|
| `06 01` → `07 01 count ids…` | APK, Test | The count is validated and only the listed bytes are function IDs. The old parser scanned the whole reply, so a reply listing 19 functions (count `0x13`) advertised CODEC_INDICATOR (`0x13`). |
| Discovery success | Test | `capabilitiesKnown()` is true once a valid `07` arrives. "The device listed nothing" and "the device never answered" stay distinct. Unknown keeps the existing recognised-model fallback for the controls that predate this work. |
| `D0 slot 01` → `D1 slot fmt len title len summary type …` | APK, Test | Sent for every advertised slot D1/D2/D3. A feature is matched by its ENUM_NAME title (`TOUCH_PANEL_SETTING`, `MULTIPOINT_SETTING`) and must be BOOLEAN_TYPE with no trailing data. LIST_TYPE is never coerced. Lengths are UTF-8 byte counts ≤ 128. **No slot is assumed.** |
| `D2 slot` → `D3/D5 slot a`, `D6 slot` → `D7/D9 slot 01 v` | APK, Test | Queued once a slot is resolved. `a`: 00 available, 01 unavailable (opposite polarity to `v`). |
| `F0 03` → `F1 03 type` | APK, Test | Only when function `F3` is advertised. Only type `00` (ProtocolSettingType.ON_OFF) is accepted. |
| `F2 03` → `F3/F5 03 a`, `F6 03` → `F7/F9 03 00 v` | APK, Test | Wearing control availability and value. Availability is not a worn/not-worn sensor. |
| Dependent queries | Test | The Listener only enqueues follow-up requests; the command worker (`sendPendingQueries`, driven by the controller's pump) sends them, so the reader stays free to receive their ACKs. |
| Session scope | Test | Disconnect forgets the function set, slots, availability, values and queued queries. Reconnect rediscovers; nothing from the old session is replayed. |

Not decoded on purpose: the upscaling effect indicator (`15`/`17`) is routed away from the DSEE
setting and ignored — "DSEE enabled" and "DSEE processing right now" are different facts.

## Read-back for existing settings

Each read is queued when the `07` reply advertises the matching function, and again after a write
to that setting is ACKed. A different reported value replaces the request on screen; the status
says Confirmed only once the device has reported. Without a valid `07` nothing is read and writes
stay "Sent".

| Setting | Function | Read → reply / notification | Evidence | Notes |
|---|---|---|---|---|
| NC / ambient | 62 | `66 02` → `67/69 02 effect ncType ncValue asmType asmId level` | HW (read), APK | Every field is kept. The mode comes from the NC type and value, not only the effect and level: a device can report noise cancelling while still carrying its last ambient level. For the level-adjustment type (what the client writes) level 0 also means NC, the rule the hardware session relied on. Still read once on connect without needing `07`, as before. No extra read after writes — the XM4's notifications were seen to follow. |
| Equalizer | 51 | `56 01` → `57/59 01 preset count levels…` | APK (layout), GB (bias) | Count validated. Levels decode only for clear bass + 5 bands, with the existing +10 bias; another shape keeps the old bands and disables the band editor. A reported `FF` is recorded as Manual (the client's own write marker). Gain conversion and persistent Custom 1/2 editing are **not** settled and were not changed. |
| VPT / sound position | 41 / 42 | `46 01` / `46 02` → `47/49 type preset` (table 1) | APK | Each card is gated on its own function. Table-2 `47/49` is voice guidance. |
| Speak-to-Chat toggle | F5 | `F6 05` → `F7 05 00 v` (RET), `F9 05 01 v` (NTFY) | APK | Different sub-types for RET and NTFY. `F9 05 02 v` is a preview and never changes the setting. |
| Speak-to-Chat details | F5 | `FA 05` → `FB/FD 05 00 sens focus exit` | APK (inferred from the `FC` body) | Exit times are FAST/MID/SLOW/NONE; the UI no longer claims durations. |
| DSEE | E2 | `E6 02` → `E7/E9 02 00 v` | APK, GB | The setting only. `E7/E9 01` (connection priority) is not decoded yet. |
| Voice guidance | 39 | `46 01 01` on table 2 → `47/49 01 01 v` | APK (RET/NTFY), GB (GET) | Inquiry and detail bytes validated. |
| Auto power-off | F4 | `F6 04` → `F7/F9 04 01 active selected` | APK | Both elements are kept. Choosing "off" or "when removed" keeps the selected timer instead of overwriting it with `00`. Choices offered: the model's known list (XM4: off / when taken off — from Gadgetbridge and the earlier XM4 notes, not a capture) plus anything the device reports. The `F1 04` capability layout is not recovered. |

Not done here: the EQ (`50/51`), VPT (`40/41`), audio (`E0/E1`) and auto power-off (`F0/F1 04`)
capability layouts were not recovered, so ranges and choices come from the reported state and the
model, not from capability metadata.

## Touch panel and wearing control

New controls on the System screen. Neither write has been sent to a headset.

| Control | Write | Evidence | Notes |
|---|---|---|---|
| Touch panel | `D8 slot 01 v` | APK, Test | `slot` is the one whose `D1` reply named `TOUCH_PANEL_SETTING`; the builder refuses anything outside D1–D3. Enables or disables the touch panel. It does not remap gestures. |
| Wearing control | `F8 03 00 v` | APK, Test | Only after `F3` was advertised and `F1 03` reported type `00`. This is the setting that lets the headset act on wearing detection. It is not a reading of whether the headphones are being worn. |

A write is accepted only when all three are true: the device described the control, its last
availability report said available, and it has reported the current value. Before that the row
says it is reading from the headphones, and it has no switch. The gate is checked again when the
frame is built. If an availability notification has arrived in between, the request fails and
nothing is sent. After the ACK the control is read again (`D6 slot` / `F6 03`). The status says
Confirmed only once that read answers, and a value the device did not apply replaces the request
on screen. A request still pending when the session ends is dropped, not replayed after
reconnect. Writes after reconnect go to whichever slot the new discovery reports.

## XM4 hardware pass

Not yet run. To be completed on a WH-1000XM4 before any APK-only behaviour is described as
working:

1. Record model, firmware version and the Aural commit (`About` shows it).
2. Capture the `07` reply on connect. Every later step depends on it: if the XM4 does not
   answer `06 01`, discovery stays Unknown and the new read-backs do not run.
3. Capture each `D1` reply: which slot carries `TOUCH_PANEL_SETTING` / `MULTIPOINT_SETTING`.
4. Capture the `F1 03` reply.
5. For each existing setting: read on connect → change it in Aural → confirm the chip reaches
   Confirmed → change it with a physical button or Sony's app → confirm Aural follows.
   Specifically check that the `57` equalizer reply has 6 levels with the +10 bias, the
   Speak-to-Chat `FB` layout, and the voice-guidance `46 01 01` read.
6. Auto power-off: capture `F7 04` with each mode, and whether the XM4 really offers no timers.
7. Touch panel: confirm the slot from step 3, then turn it off in Aural. Check that `D8 slot 01 00`
   is ACKed, that the `D7` read reports `00`, and that touching the ear cup does nothing. Turn it
   back on. Repeat the change from Sony's app and check that Aural follows the `D9` notification.
   Note any condition under which `D3/D5` reports unavailable.
8. Wearing control: turn it off and on in Aural and capture `F8 03 00 v` → ACK → `F7 03 00 v`.
   Record what the headset actually does differently (pausing on removal, and anything else),
   so the label can describe it.
