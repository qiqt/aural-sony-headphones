# Sony V1 protocol

These notes describe the Sony Tandem V1 (MDR) messages Aural sends and reads.
Related references are the vault note
*Sony Sound Connect 13.3.0 – Protocol Findings* and the reference implementation in
`protocol-reference/xm4_remaining.py`.

## Sources and testing

### XM4 manual testing, 2026-11-02

The owner tested the existing controls on their WH-1000XM4, including setting changes
and multipoint, and reported that they worked. Firmware and build numbers weren't
recorded for each check. Controls added after November 2 still need separate testing.
The tables below also track packet captures and details that need further investigation.

| Tag | Meaning |
|---|---|
| **HW** | Seen on a real WH-1000XM4. Unless noted otherwise this means the protocol traces captured on 2026-10-31 (firmware not recorded); their frames are fixtures in `test_protocol.cpp`, `test_discovery.cpp` and `test_multipoint.cpp`. |
| **APK** | Recovered statically from Sound Connect 13.3.0 serializers, decoders and callers. No capture. |
| **GB** | Byte layouts and value mappings cross-checked against [Gadgetbridge's V1 protocol implementation](https://codeberg.org/Freeyourgadget/Gadgetbridge/src/branch/master/app/src/main/java/nodomain/freeyourgadget/gadgetbridge/service/devices/sony/headphones/protocol/impl/v1/SonyProtocolImplV1.java). |
| **Test** | Covered only by synthetic tests against `FakeBluetoothConnector`. |
| **Owner** | Manual testing on the owner's XM4, reported 2026-11-02. |

Tests with `FakeBluetoothConnector` use scripted replies. Hardware results are marked
**HW** for packet captures or **Owner** for manual checks.

## Capability-driven additions, 2026-11-02

This section supersedes historical statements below that EQ/power-off capability layouts,
connection preference, codec/activity indicators and assignment parsing are unimplemented.
The implementation uses `EqualizerProtocol.h`, `AudioProtocol.h`, and `AssignmentProtocol.h`.
The November 2 hardware check read settings without changing them.

### Equalizer and auto power-off

- `50 01` -> `51 01 bandCount levelSteps presetCount {presetId nameLength nameBytes}*`.
  The XM4 reports six levels, 21 steps and twelve preset IDs. Sony's signed level conversion is
  `raw - ((levelSteps - 1) / 2)`. This establishes control steps, not an acoustic dB measurement.
- Preset choices and slider ranges follow this capability. The current five-band plus Clear Bass
  editor requires six valid reported levels; unsupported shapes cannot produce custom writes.
- `F0 04` -> `F1 04 count elementIds*`. The XM4 reports `10 11` (when removed / disabled).
  A received capability replaces the model fallback; an unadvertised timer is not offered.
- Save to Custom 1/2 follows Sony's observed application workflow: select A1/A2 with zero levels,
  wait for matching EQ read-back, send `58 01 FF count levels...`, then verify the resulting curve.
  Pending saves expire on timeout or disconnect. Saving across headset restarts still needs testing.

Sony evidence: EQ schema `ev_2f5a6b18b52b743b5e7206e81e5c393079ae1462b155cebc999efb3273bb3da4`;
power-off `ev_90a799e6abd41a78eae31ffd2ec43272b3cbaaf314a32e88cfcedb571d1e49c8`;
gain/FF sender `ev_6d9dab53139ed782b391c963abf0877b9971acbf03f1cc2b05e0757d716a3111`;
custom selection/observer/edit sequence `ev_e4e47bc2c25437418344273df54b45556c231ac17adf81b4b826be3b6a750158`.
All are retained in the reverse-engineering lab's `sony-evidence` directory.

### Connection preference, codec and DSEE activity

| Operation | Bytes (table 1) | Meaning |
|---|---|---|
| Connection capability | `E0 01` -> `E1 01 00` | SOUND_CONNECTION type |
| Availability | `E2 01` -> `E3/E5 01 a` | 00 available, 01 unavailable |
| Read preference | `E6 01` -> `E7/E9 01 00 v` | 00 sound quality, 01 stability |
| Write preference | `E8 01 00 v` | Requires capability, availability and current value |
| Read codec | `18 00` -> `19/1B 00 codec` | 00 unsettled, 01 SBC, 02 AAC, 10 LDAC, 20 aptX, 21 aptX HD, FF other |
| Read DSEE activity | `14 00` -> `15/17 00 type status` | type 00 HX, 01 DSEE, 02 HX AI; status 00 OFF, 01 VALID, 02 INVALID |

System shows codec and DSEE activity independently of the DSEE enabled setting. Only activity
status 01 displays Processing, matching Sony's indicator logic. Unknown readings remain unknown.
Availability is rechecked before sending a connection-preference change. Aural then
reads the setting to check whether the change took effect.

Sony evidence: `ev_6136aa20c0df0bace8a3901907c7abae043e28a10ff91406eadbf7d78a565893`,
`ev_79d8bd330ee3f8a9000e8826b9dd1ac18822e45361abd5e85da07eb7e0027f22`,
`ev_a70f99d748ea6b28d8dfa676b9c8a88eb50de523c2ecd20e39015de7c6a6034c`.

### Button assignments

Capability `F0 06` -> `F1 06 keyCount {keyId keyType defaultPreset presetCount
{presetId actionCount {action function}*}*}*` defines the ordering and allowed options.
Read `F6 06` -> `F7/F9 06 count presetIds*`; availability `F2 06` ->
`F3/F5 06 count statuses*` (00 available, 01 unavailable).
Writes are `F8 06 count presetIds*` in the original capability order, preserving every other key.
There are no key IDs in the SET payload. Aural checks both capability membership and availability.

The current XM4 describes one Custom button, with Ambient sound control, Google Assistant and
Amazon Alexa presets. The UI displays the actions from the returned capability. This is preset
assignment, not arbitrary gesture remapping; assistant setup on a phone may still be required.
Sony sources: `wm0/c` -> `wm0/b` -> `wm0/d` capability records; `vm0/e` availability;
`l00/b.p` and `.f` preserve capability-order indexes.
Retained assignment evidence: `ev_0e8af1c0458bae5e8a50d00819ac91cf456c4171b9140850cd97577e3a0b5836`,
`ev_718b7c42944f9042c6b0264269556cac843e5b8715a2cd68638931b1029129ad`,
`ev_f46fabdad2fbea3b9b4e054782b64d08e4d0841b74286c7523165d3aa1c37747`.

Sanitized live GET replies are retained in `Client/tests/fixtures/xm4-capabilities-20261102.json`.
They confirm AAC, sound-quality priority, inactive DSEE, and the assignment schema above on
firmware 2.9.0. Synthetic tests exercise writes, malformed packets, unavailable states, read-back
and session reset. The new controls still need manual testing.

The integrated engine was also exercised read-only against the same headset. Capture:
`Client/tests/fixtures/xm4-integrated-readonly-20261102.json` (174 frames). It decoded the six-band,
21-step, twelve-preset EQ capability; two power choices; connection preference 00 and available
status `E3 01 00`; AAC; inactive DSEE; and one assignable control. Outgoing payloads were audited
as initialization/GET requests and transport ACKs only. No new feature write was tested on hardware.

Tests cover capability bounds, mismatched tables, per-key availability, read-back,
disconnect/reset, Custom-slot selection before curve transmission, changes from other
apps, and save timeouts.

## Protocol trace

Set `AURAL_TRACE=1` before starting Aural to append every frame sent and
received to `%TEMP%\aural-trace.log`. Set it to a path to write somewhere else. The log also
records connects and disconnects, missing ACKs, and every reply no decoder accepted ("not used").
The Qt app adds what the UI offered after each capability change, and any request a gate refused.
The trace includes the headphones' address and the names of their paired devices.

## Framing and dispatch

| Item | Evidence | Notes |
|---|---|---|
| `3E escape(type seq len32 payload checksum) 3C` | HW, APK | Unchanged transport. |
| Connect handshake (`00 00`, then battery, `06 01`, `66 02`, table-2 `36 01`) | HW | Once, a WH-1000XM4 left the first `00 00` on a new link unacknowledged, while a fresh connection a few seconds later answered within 125 ms. The handshake used to swallow the missing ACK. The app then showed "Connected" over a link that refused everything, until the first change failed. Now the session ends at once, and the Qt app reconnects once on its own before reporting "didn't respond". That one retry after a normal connect has not been needed on hardware since. The same silence came back once while reconnecting after a multipoint change (see multipoint mode); that check runs on its own schedule and got through. The INIT reply `01 00 70 00` is not decoded. |
| Declared length must match the frame | APK, Test | `unpackBtMessage` now reads the full big-endian length and rejects frames whose length disagrees with the bytes present. It used to trust only the low byte, which read past the end of a short frame and truncated payloads over 255 bytes. |
| Tables | APK, Test | `0C` = table 1 (DATA_MDR), `0E` = table 2 (DATA_MDR_NO2). Replies dispatch on table first, then command, then selector. Each decoder re-checks the table, so a table-1 `37` (UPDT_RET_PARAM) can never reach the table-2 paired-device decoder. |
| Malformed input | Test | Every decoder checks command, selector, exact length and enumerated values before reading. A frame that fails leaves state untouched. |

## Capability discovery

| Message | Evidence | Notes |
|---|---|---|
| `06 01` → `07 01 count ids…` | HW, APK, Test | The count is validated and only the listed bytes are function IDs. The old parser scanned the whole reply, so a reply listing 19 functions (count `0x13`) advertised CODEC_INDICATOR (`0x13`). The XM4 lists 23: `71 38 62 F5 81 51 A1 E1 E2 D2 F6 D1 F4 F3 39 12 13 11 30 C1 14 22 21`. That is no VPT (`41`) and no sound position (`42`), so Spatial Sound is no longer offered on it. `14 21 22 30 71 A1 C1` are not used by Aural; their names are under "Seen on the XM4, not used". |
| Discovery success | Test | `capabilitiesKnown()` is true once a valid `07` arrives. "The device listed nothing" and "the device never answered" stay distinct. Unknown keeps the existing recognised-model fallback for the controls that predate this work. |
| `D0 slot 01` → `D1 slot fmt len title len summary type …` | HW, APK, Test | Sent for every advertised slot D1/D2/D3. A feature is matched by its ENUM_NAME title (`TOUCH_PANEL_SETTING`, `MULTIPOINT_SETTING`) and must be BOOLEAN_TYPE. LIST_TYPE is never coerced. Lengths are UTF-8 byte counts ≤ 128. **No slot is assumed.** On the XM4, touch panel is D1 and multipoint is D2. Both records end `01 00`: one byte after the type, which the APK-derived reference used to reject. Aural and the reference now accept it; what the byte means is not known. |
| `D2 slot` → `D3/D5 slot a`, `D6 slot` → `D7/D9 slot 01 v` | HW, APK, Test | Queued once a slot is resolved. `a`: 00 available, 01 unavailable (opposite polarity to `v`). The XM4 replied `D3 D1 00` and `D7 D1 01 01` (touch panel available, on), and `D3 D2 00` and `D7 D2 01 01` (multipoint available, on). |
| `F0 03` → `F1 03 type` | HW, APK, Test | Only when function `F3` is advertised. Only type `00` (ProtocolSettingType.ON_OFF) is accepted. The XM4 replies `F1 03 00`. |
| `F2 03` → `F3/F5 03 a`, `F6 03` → `F7/F9 03 00 v` | HW (reads), APK, Test | Wearing control availability and value. Availability is not a worn/not-worn sensor. The XM4 replied `F3 03 00` and `F7 03 00 01`. |
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
| NC / ambient | 62 | `66 02` → `67/69 02 effect ncType ncValue asmType asmId level` | HW (read, write, notifications), APK | Every field is kept. The mode comes from the NC type and value, not only the effect and level: a device can report noise cancelling while still carrying its last ambient level. Aural writes the level-adjustment type (`68 02 11 01 …`). The XM4 answers each write with a `69` notification that uses the dual/single/off type (`02`): `69 02 01 02 02 01 00 00` for NC, `69 02 01 02 00 01 00 0A` for ambient level 10, and `69 02 00 02 02 01 00 00` for off. Still read once on connect without needing `07`. There is no extra read after a write, because the XM4's notification follows. |
| Equalizer | 51 | `56 01` → `57/59 01 preset count levels…` | HW (read, preset write), APK (layout), GB (bias) | Count validated. Levels decode only for clear bass + 5 bands, with the existing +10 bias; another shape keeps the old bands and disables the band editor. A reported `FF` is recorded as Manual (the client's own write marker). The XM4 reports 6 levels (`57 01 11 06 12 09 0B 0A 0D 0F`) and answers a preset write (`58 01 12 00`) with `59 01 12 06 …`. Gain conversion and persistent Custom 1/2 editing are **not** settled and were not changed. |
| VPT / sound position | 41 / 42 | `46 01` / `46 02` → `47/49 type preset` (table 1) | APK | Each card is gated on its own function. Table-2 `47/49` is voice guidance. The XM4 advertises neither, so it gets no Spatial Sound screen. |
| Speak-to-Chat toggle | F5 | `F6 05` → `F7 05 00 v` (RET), `F9 05 01 v` (NTFY) | HW, APK | Different sub-types for RET and NTFY. `F9 05 02 v` is a preview and never changes the setting. On the XM4, the read after a write (`F8 05 01 v`) already reports the new value. `F9 05 01 v` follows about 0.85 s after the write, then `F9 05 02 00`. Turning it on also produced `F5 05 00 00`, `F5 05 00 01` (not decoded) and an ambient notification (`69 02 01 02 00 01 00 14`). Turning it off produced an NC notification. Aural shows whichever mode the headphones report. |
| Speak-to-Chat details | F5 | `FA 05` → `FB/FD 05 00 sens focus exit` | HW (read), APK (inferred from the `FC` body) | Exit times are FAST/MID/SLOW/NONE; the UI no longer claims durations. The XM4 replied `FB 05 00 00 00 00`. |
| DSEE | E2 | `E6 02` → `E7/E9 02 00 v` | HW, APK, GB | The setting only. `E7/E9 01` (connection priority) is not decoded yet. The XM4 answers a write with `E9`, the "processing now" indicator `17 00 02 v` (ignored on purpose), and an undecoded `A9 01 20 10`. |
| Voice guidance | 39 | `46 01 01` on table 2 → `47/49 01 01 v` | HW, APK (RET/NTFY), GB (GET) | Inquiry and detail bytes validated. The XM4 answers `48 01 01 v` with `49 01 01 v`. |
| Auto power-off | F4 | `F6 04` → `F7/F9 04 01 active selected` | HW (read), APK | Both elements are kept. Choosing "off" or "when removed" keeps the selected timer instead of overwriting it with `00`. The XM4 reported `F7 04 01 10 00` (when removed). Choices offered: the model's known list (XM4: off / when taken off — from Gadgetbridge and the earlier XM4 notes) plus anything the device reports. The `F1 04` capability layout is not recovered. |

Not done here: the EQ (`50/51`), VPT (`40/41`), audio (`E0/E1`) and auto power-off (`F0/F1 04`)
capability layouts were not recovered, so ranges and choices come from the reported state and the
model, not from capability metadata.

## Touch panel and wearing control

New controls on the System screen. Both writes have been sent to a WH-1000XM4, off and back on.
The tester confirmed that both worked.

| Control | Write | Evidence | Notes |
|---|---|---|---|
| Touch panel | `D8 slot 01 v` | HW, APK, Test | `slot` is the one whose `D1` reply named `TOUCH_PANEL_SETTING`; the builder refuses anything outside D1–D3. Enables or disables the touch panel. It does not remap gestures. The XM4 ACKs `D8 D1 01 v` and sends `D9 D1 01 v` straight after; the read that follows returns `D7 D1 01 v`. |
| Wearing control | `F8 03 00 v` | HW, APK, Test | Only after `F3` was advertised and `F1 03` reported type `00`. This is the setting that lets the headset act on wearing detection. It is not a reading of whether the headphones are being worn. The XM4 ACKs it and sends `F9 03 00 v` straight after; the read returns `F7 03 00 v`. |

A write is accepted only when all three are true: the device described the control, its last
availability report said available, and it has reported the current value. Before that the row
says it is reading from the headphones, and it has no switch. The gate is checked again when the
frame is built. If an availability notification has arrived in between, the request fails and
nothing is sent. After the ACK the control is read again (`D6 slot` / `F6 03`). The status says
Confirmed only once the device reports the value. That report is the reply to the read, or a
notification if one comes first, as it does on the XM4. A value the device did not apply replaces
the request on screen. A request still pending when the session ends is dropped, not replayed after
reconnect. Writes after reconnect go to whichever slot the new discovery reports.

## Multipoint mode

The multipoint on/off setting, on the System screen. This is separate from the paired-device list
on Connected Devices (table-2 `36`–`39`), whose connect/disconnect commands do not turn the mode
on or off. On a WH-1000XM4, turning multipoint off has been traced twice: once cancelled and once
accepted. The tester reports that turning it back on worked too, but no trace was running, so the
`06` alert has not been captured.

With multipoint off, the XM4 stops advertising `38` (paired-device management): its `07` reply
lists 22 functions, and `36 01` is ACKed but never answered. Aural rediscovers on every connect, so
it withdraws Connected Devices until multipoint is back on.

| Message | Evidence | Notes |
|---|---|---|
| `D8 slot 01 v` | HW (turning off), APK, Test | `slot` is the one whose `D1` named `MULTIPOINT_SETTING`. Same gate as the touch panel, plus two more conditions. The host must have installed alert handling: the Qt app does, and the legacy ImGui client never changes multipoint. And only one change can be in progress at a time. The XM4 ACKed `D8 D2 01 00`. Unlike the touch panel, no `D9` followed, and the read while the alert was open still returned `D7 D2 01 01`. |
| `94 01 00` | HW, APK, Test | Enables fixed-message alerts (CommonStatus.ENABLE). Sent once per session, immediately before the first multipoint write. Sound Connect sends it at connect; Aural waits, so the headset is not asked to send alerts until Aural needs one. The XM4 ACKed it. |
| `99 01 id action` | HW (`07 01`), APK, Test | `06` means turning multipoint on (Sony shows its LDAC caution for this one); `07` means turning it off. Action `00` asks only for acknowledgement; `01` offers accept or cancel. An alert is acted on only while this session's write is out, and only if it matches the requested direction. Any other alert is left unanswered: another message ID, no change out, the opposite direction, or a change already answered. The XM4 sent `99 01 07 01` 1 ms after ACKing the write. `06` and its action type have not been seen. |
| `98 01 id choice` | HW, APK, Test | The user's answer, quoting the ID that was received. `01` accepts and `00` cancels; Escape also cancels, as closing Sony's dialog does. Never sent without an alert and a choice from the user. The XM4 ACKed `98 01 07 00` and reported `D9 D2 01 01` 1 ms later: still on, and the link stayed up. It ACKed `98 01 07 01` and closed the link 36 ms after the answer, without a `D9`. |

A change moves through these steps: requested → written → confirming (the alert is on screen) →
answered → accepted. It ends in one of four ways:

- The device reports the requested value in `D7`/`D9`. This is the only way it is confirmed.
- The user cancels.
- The headphones neither ask nor report for 20 s. Aural stops waiting and rereads the value.
- The session ends.

A report of the old value does not end a change, because the device may be about to ask, or about
to drop the link to apply it. If the link drops while a change is out (written, being confirmed,
answered or accepted), Aural reconnects to the same headphones through the normal connection
path. It makes its first attempt after 1.5 s, retries every 3 s, and gives up after 60 s. It then
rediscovers the setting and reads it, and says whether the headphones applied the change. It never
resends the change or the answer. If the user disconnects, cancels the reconnect, or picks other
headphones, the check stops.

Measured on the XM4 after accepting a turn-off, counting from the answer:

| Time | What happened |
|---|---|
| 0.04 s | The headphones closed the link. |
| 1.6 s | First attempt failed within 0.14 s (Windows error 10060). |
| 4.7 s | Second attempt connected, but the headphones never ACKed `00 00`. They sent start-up notifications instead (`A5 01 00 02`, `1B 00 02`, `A9 01 20 0B` twice, `A9 01 00`; none decoded). The session ended after the 2 s ACK timeout. |
| 8.5 s | Third attempt: normal handshake, 274 ms to the first ACK. |
| 9.4 s | `D7 D2 01 00` read, and Aural reported "Multipoint is now off." |

That is well inside the 60 s limit. The silent second link matches the unanswered `00 00` seen on
the very first connection: right after a link opens, the headphones may not be ready.
`controller_multipoint_check_survives_a_failed_connect_and_a_silent_handshake` replays this
sequence.

## Seen on the XM4, not used

The names come from the Sound Connect 13.3.0 enums in the byte catalog. The catalog has no field
layouts for these messages, so each reading below rests on which enum value a byte matches. Aural
ignores all of them (the trace marks them "not used"), and none gets a control without more
evidence.

| Bytes | Command | Reading | Seen |
|---|---|---|---|
| `01 00 70 00` | CONNECT_RET_PROTOCOL_INFO | The reply to `00 00`. Layout not recovered; `70` may be a protocol version. | Every connect. |
| `1B 00 02` | COMMON_NTFY_AUDIO_CODEC | `02` is AAC. | On a new link after a multipoint change. |
| `A5 01 00 v` | PLAY_NTFY_STATUS, PLAYBACK_CONTROLLER | `v` matches PlaybackStatus: `01` play, `02` pause. The `00` is not recovered. | On a new link (`02`), and unprompted later (`01`). |
| `A9 01 20 v` | PLAY_NTFY_PARAM, PLAYBACK_CONTROLLER | `20` is VOLUME, so `v` is the volume (16 and 11 were seen). | After each DSEE write, and on a new link. |
| `A9 01 00` | PLAY_NTFY_PARAM, PLAYBACK_CONTROLLER | `00` is TRACK_NAME, with nothing after it. | On a new link. |
| `F5 05 00 v` | SYSTEM_NTFY_STATUS, SMART_TALKING_MODE | Not recovered. `00` and then `01` arrived just before Speak-to-Chat switched to ambient, so it may be the conversation state. | Turning Speak-to-Chat on. |

Advertised functions Aural does not use: `14` BLE_SETUP, `21` POWER_OFF, `22` CONCIERGE_DATA, `30`
FW_UPDATE, `71` AUTO_NC_ASM (Sony's adaptive sound control), `A1` PLAYBACK_CONTROLLER and `C1`
ACTION_LOG_NOTIFIER.

## XM4 hardware pass

### Read-only follow-up, 2026-11-02

Run without any GUI automation using `Client/tools/headset_probe`. The headset was already
connected to Windows; the probe opened and closed its own RFCOMM control socket. It sent
initialization and GET requests plus transport ACKs, with no settings writes or alert responses.
Source: HEAD `4993626` with local modifications. The sanitized frame capture, original-log digest
and probe-source digest are preserved in `Client/tests/fixtures/xm4-readonly-20261102.json`.

- Model reply: `05 01 0A 57 48 2D 31 30 30 30 58 4D 34` (WH-1000XM4).
- Firmware reply: `05 02 05 32 2E 39 2E 30` (2.9.0).
- Battery: 10%, charging byte 00. NC on; EQ preset 11 with six levels;
  Speak-to-Chat off; DSEE off; voice guidance on; power-off active 10, selected timer 00.
- Touch panel: slot D1, available, on. Wearing control: available, on.
  Multipoint: slot D2, available, off. The function list contains 22 entries and excludes 38,
  consistent with the earlier observation that paired-device management is absent with multipoint off.
- EQ capability GET `50 01` returned
  `51 01 06 15 0C 00 00 10 00 11 00 12 00 13 00 14 00 15 00 16 00 17 00 A0 00 A1 00 A2 00`.
- Power-off capability GET `F0 04` returned `F1 04 02 10 11`.

The last two replies contain two XM4 power choices, six EQ levels and twelve presets.
Their decoded layouts are described under Equalizer and auto power-off above.
Custom 1/2 persistence across restarts still needs testing.

This check read settings on firmware 2.9.0. The October traces didn't record firmware.

### October hardware pass and remaining checks

Partly run, in three traces on 2026-10-31. The first covers steps 2–4, most of step 5, and the
reads in steps 6 and 8. It also found the two bugs described above: the unacknowledged first
`00 00`, and the byte after BOOLEAN_TYPE that hid the touch panel and multipoint rows. The second,
with both fixed, covers steps 7 and 8 and cancelling a multipoint turn-off. The third accepts one.

| Step | Result |
|---|---|
| 1 | Firmware not recorded. Aural `4b8fe67` plus the uncommitted fixes. |
| 2 | Done: 23 functions, listed under capability discovery. |
| 3 | Done: touch panel D1, multipoint D2, no D3. |
| 4 | Done: `F1 03 00`. |
| 5 | Reads on connect and writes from Aural for NC/ambient, equalizer preset, Speak-to-Chat, DSEE and voice guidance. Not yet done: changes made on the headphones or in Sony's app, and band edits. |
| 6 | One mode only: `F7 04 01 10 00`. |
| 7 | Off and back on from Aural, each confirmed by `D9` and the `D7` read (see the touch panel table). The tester reported it working. Not yet done: a change from Sony's app, and anything that makes it unavailable. |
| 8 | Off and back on from Aural, each confirmed by `F9` and the `F7` read. What the headset does differently with it off is not recorded. |
| 9 | Turning it off, done both ways. Each time: `94 01 00`, `D8 D2 01 00`, then `99 01 07 01`. Cancel: `98 01 07 00`, then `D9 D2 01 01` (still on, no disconnect). Continue: `98 01 07 01`, the link drops, Aural reconnects on its own and reads `D7 D2 01 00`. Turning it on: reported working, not traced, so the `06` alert, its action type and the LDAC check are still uncaptured. |
| 10 | Done for turning it off (see the timing table under multipoint mode). The alert arrived 27 ms after the write was sent. The user took 4 s to answer, and the XM4 waited. |

Checklist for further XM4 captures:

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
9. Multipoint, with two source devices paired:
   - Confirm the slot from step 3, then turn multipoint on in Aural.
   - Capture `94 01 00`, then `D8 slot 01 01` → ACK → `99 01 06 a`, and record the action type `a`.
   - Accept, and capture `98 01 06 01`. Record whether and when the link drops.
   - Check that Aural reconnects on its own and reports the value it reads afterwards. Check
     whether LDAC was switched off.
   - Repeat turning it off (`07`). Then repeat once more and press Cancel: expect `98 01 06 00`
     and no change.
   - Note any alert other than `06`/`07` that the headset sends once alerts are enabled.
10. Time how long the headset takes to answer the write, to drop the link, and to come back.
    Compare these with Aural's limits (20 s waiting for an answer, 60 s reconnecting, 15 s to read
    the value).

## Diagnostics, feedback, and broader V1 verification (2026-11-02)

V1 initialization now reads model (`04 01`) and firmware (`04 02`). The decoder
accepts table-1 `05 selector length ASCII-bytes` only, with exact length and no
control characters. Captured XM4 replies are `05 01 0A 57 48 2D 31 30 30 30 58 4D 34`
and `05 02 05 32 2E 39 2E 30`. These strings are informational: they neither change
the selected transport nor authorize extra controls. Session reset clears them.

System diagnostics offers an explicit JSON export with build/device details,
capability IDs, setting states, audio indicators, and a bounded recent process log.
No environment variable or visible shell is needed. Nothing is uploaded.

Setting chips distinguish sending, sent-but-unconfirmed, confirmed, and inability
to apply a change. Hover/accessibility descriptions explain the difference and
retry action. Failed transitions produce a setting-specific notification. Connection
preference and assignment selectors retain the requested choice through ACK while
the reported-value getters stay unchanged; read-back settles the UI to what the
device reports. These changes do not introduce automatic write retries.

The generic `--read-v1` probe allows collecting other already-connected V1 devices.
Synthetic controller profiles cover WH-1000XM3, WF-1000XM3, and an unrecognized
name with a three-level EQ and restricted power choices. They verify that capability
replies enable the available preset controls, disable the incompatible six-level
editor, and hide unavailable functions. These profiles use scripted device replies.

At the end of the earlier V1 pass, XM5/XM6 had transport isolation only.
The subsequent multi-model implementation adds V2 startup, EQ, ANC and audio
controls; see the implementation status in PROTOCOL-MULTIMODEL.md. Legacy setters
remain separate from V2. XM5 and XM6 still need testing on real headphones.

Controller tests cover diagnostics export/reset, alternate V1 profiles and
separate V1/V2 command handling.

## Multi-model REA follow-up

See [XM3/XM5/XM6 implementation research](PROTOCOL-MULTIMODEL.md) for the
2026-11-02 Sony service-discovery and V2 startup findings, generation-specific
payload differences, evidence IDs, implementation order, and unresolved hardware
questions. The [V2 enum catalog](docs/sony-v2-byte-catalog.json) contains 34 enums
and 539 entries used across Sony's app. Check each device's replies for its supported functions.
