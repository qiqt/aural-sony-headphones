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

## XM4 hardware pass

Not yet run. To be completed on a WH-1000XM4 before any APK-only behaviour is described as
working:

1. Record model, firmware version and the Aural commit (`About` shows it).
2. Capture the `07` reply on connect. Every later step depends on it: if the XM4 does not
   answer `06 01`, discovery stays Unknown and the new read-backs do not run.
3. Capture each `D1` reply: which slot carries `TOUCH_PANEL_SETTING` / `MULTIPOINT_SETTING`.
4. Capture the `F1 03` reply.
