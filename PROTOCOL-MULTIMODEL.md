# XM3, XM5 and XM6 implementation research

Research from 2026-11-02 on adding XM3, XM5 and XM6 support.
These models still need testing on real headphones.

## Sources

The research used the Sony Sound Connect 13.3.0 disassembly in the REA-Linux VM.
Python scripts read the smali files; the Android app wasn't run.

Input APKS SHA-256: `7901e52cf38c5ebd0d5be760e2b105df7ed7ab85c8e732dd4481c910f8fda31e`.
VM root: `/home/joe/analysis/sony-13.3.0/smali/classes4` unless otherwise stated.
The package signature hasn't been checked.

Raw evidence and readable extracts are under
`C:/Users/User/Documents/ChatGPT/Reverse Engineer/sony-evidence` and its parent.
Evidence IDs below link findings to the disassembled classes.

## Protocol selection

Use one app and shared device state with separate V1 and V2 protocol adapters.
Treat WH-1000XM3 as an extension of the existing legacy implementation, and XM5/XM6
as candidates for the V2 adapter. Select the adapter using the device's
advertised service and handshake.
Model-specific feature sets must come from the connected device's capabilities.

Sony itself performs service discovery. `ep0/a.a` reads `BluetoothDevice.getUuids`;
`ep0/a.d` iterates `lm0/i.c` providers, V2 first and V1 second. `ep0/a.c` checks normal
and byte-reversed UUIDs as an Android workaround. The services are:

- V1: `96CC203E-5068-46AD-B32D-E316F5E069BA`
- V2: `956C7B26-D49A-4BA8-B03F-B17D393CB6E2`

Evidence: `ev_d3aee2de7c05d09c9c1604efb3fffab02156b35c64f82d3bf60e69a247f15fbd`,
`ev_6f62f3398e73d93fe72f2d265d40b8612a34fc66860b316c9062c0ab1135e1f6`.
Check the advertised services on each model and firmware. The reversed-UUID
workaround is Android-specific; Windows testing would need to show the same issue
before adding it here.

## What should remain shared

- RFCOMM connection ownership, cancellation, reconnect/session invalidation.
- Framing and the established transport acknowledgement layer, with protocol
  generation kept distinct from the per-frame command table.
- Normalized device state, capabilities, requested/reported values and UI statuses.
- Read-back scheduling and the rule that ACK alone never confirms a setting.
- Diagnostics/export and the UI components that render supported controls.

Sony's `om0/e` chooses `om0/g` for TABLE_SET_1 and `om0/f` for TABLE_SET_2, while
both use `qm0/a`, `qm0/b` and `lm0/c`. The selected decoder then dispatches that
generation's table1/table2 messages. TABLE_SET_2 means the V2 generation; it does
not mean the V1 DATA_MDR_NO2 frame table. Sharing a command number is insufficient
for sharing its decoder. Evidence: `ev_1435e8470bee530763018fa897cb1eed91ebb49f0394c3d5afce94b81ecaa5cc`
and `ev_97cf42185326720d4486ff34cedca8c0462bef89c932c62f7f83ccd76d516ab9`.

The V2 dispatcher also recognizes SHOT and LARGE_DATA_MDR variants. Reusing the
frame envelope does not justify ACKing or processing these like ordinary DATA_MDR.
Handle unsupported types deliberately, and port their semantics only when needed.
Evidence: `ev_71d3b39876891ad0abe213fbb424f18978dc27f2c290e3880baf2fd74ff0e39d`.

## V1/XM3 work

Most of the V1 code can be reused. ANC/ambient capability handling needs extending.

`tm0/d2$c` parses the combined V1 capability as:

```text
61 02 ncSettingType ncStepCount asmSettingType entryCount { asmId stepCount }*
```

`tv/a.m` passes these fields into Sony's DeviceCapabilityTableset1Builder. Query,
state and write remain in the V1 NCASM family, but the allowed representation and
range are capability data. Evidence:
`ev_fd78c20e1359126aae01830462cb921e67ce2e752ea0e3762c45b86043ce52e0`,
`ev_cddf0d26ed630033c7ed7535fbc31557b10b84cbb6996dfbed753c068cfc14f4`.

Implementation consequences:

1. Decode ANC/ambient capability and availability before enabling generic controls.
2. Preserve the returned NC type/value and supported ambient IDs; use the relevant
   wire representation when editing one field.
3. Replace fixed ambient bounds and generic voice-focus rules with supported ranges
   and IDs. The exact slider-to-wire mapping needs its own higher-level trace or
   model capture; a step count alone does not establish that mapping.
4. Continue using advertised EQ presets/ranges, power choices and button ordering.
5. Keep absent functions absent. Recognition of an XM3 name must not implicitly
   grant XM4's multipoint, wearing, touch or Speak-to-Chat controls.
6. Capture replies from a real XM3 and add them to the tests. Current XM3 tests
   use scripted replies.

Relevant current source: `Client/CommandSerializer.cpp` has the fixed
`MAX_STEPS_WH_1000_XM3 = 19` and derives NC dual/single mode from ambient level;
`Client/Headphones.cpp` retains the full NC report but still uses legacy setting
construction; `HeadphonesController::featureAvailable` allows recognized-model
fallback when supported functions are unknown. These are the areas to extend.

## Recovered V2 startup

`vv/e$e.H` follows a protocol-aware startup path:

1. Request protocol information with table-1 `00 00`.
2. `in0/k$b` validates an eight-byte reply:
   `01 00 version[4] table1Enabled table2Enabled`.
3. Validate the supported protocol version and retain the table-enable flags.
4. Request capability information with `02 00` on the ordinary path. Sony also
   contains a version-specific MFi branch; do not generalize its behavior to Windows.
5. Query model name, firmware version, and series/color information.
6. Request supported functions with `06 00` for each enabled table. V1 currently
   uses `06 01`, so sharing the first request does not make the sequences compatible.

Sources: `in0/c$b`, `in0/k$b`, `vv/e$e`.
Evidence: `ev_e0d9cb91ca49fda390b7870a9cff94d93d4f30a553164df96d463191635cf090`,
`ev_f1b60d4cdf4cd9e5f4f6ef6e61063867959e7c92f756d34c25a1d29ddf56f2e5`,
`ev_28c610e4bf765ab3507862b0f8c6a38f1cd8faf6d24bdccb2fb0aba82d776301`.

The version checks, capability-info payload and error handling also need implementation
and tests.

## Concrete V2 incompatibilities

The recovered command/selector catalog and serializers show why V1 packets must
not be sent through the V2 service. Available features vary by model and firmware.

| Area | Existing V1 path | Recovered V2 path |
| --- | --- | --- |
| Supported-function records | Single function-ID bytes | Counted pairs: function ID plus ordering metadata; preserve both |
| Preset EQ | Selector `01` | Selector `00`; `58 selector preset count levels...` |
| Separate custom EQ | Existing manual/preset flow | Selector `31`; `58 31 count levels...`, without preset ID |
| ANC/ambient | Combined selector `02` | Distinct variants such as `11` through `19`, plus `21`/`22`; parse the advertised variant |
| Auto power-off | SYSTEM family `F6/F7/F8/F9`, selector `04` | POWER family `26/27/28/29`, selectors `04`/`05` |
| Connection preference | AUDIO selector `01` | AUDIO `00`, with additional LDAC/Classic-LE variants |
| DSEE configuration | AUDIO selector `02` | AUDIO `01`, plus a separate variant with disable reasons |
| Button assignment | SYSTEM selector `06` | SYSTEM `03`, plus limited assignment variant `0E` |
| Wearing | Existing negotiated V1 setting | SYSTEM `06` is a wearing detector; distinguish status from control |
| Paired-device management | Table 2 peripheral selector `01` | Table 2 peripheral selector `00` or extended variant `02` |

Evidence for enum values: `ev_b7012e9c29bd46abd87737c4c5e83adbcd9d437c9e2af437279de497abea06b0`.
The [V2 catalog](docs/sony-v2-byte-catalog.json) contains source hashes and
instruction lines; the original is retained in the lab. Do not derive SET payloads solely from that enum catalog:
serializers and capability constraints are required as well.

Serializer evidence: `ev_f5a63be6a18dcf40471379cacdf828d34e61ff711b267d1ca7b6dabaf363fb7a`
and `ev_9af771f6438111123c5c1e618c41b562f40233e8514eb9cd965f3fe5779eb2e6`.
Supported-function parsing/order evidence: `ev_b113c5d4a11de240f973a35ba71f0212597a72810262f8523d7a69dc4554002c`
and `ev_162ecfb94771223308d93862ab7b76ad47607471712c5ba9c36f86b2ddac11fe`.
Sony's separate `fromEqBandInformationTableSet1/2` adapters into common EQ domain
types use the same approach (`ev_5e36898197216b8a13afbc1dc95ad3a794ff243f0e62d1f21225d549047c11aa`).

A V2 custom-EQ capability has a compact fixed layout distinct from preset EQ.
The current six-level editor and the V1 Custom 1/2 save sequence cannot be assumed
to describe every V2 EQ variant. Use the device's selected inquiry type and recovered
UI semantics before interpreting its bands or claiming persistent saves.

## Architecture and implementation order

Proposed structure:

```text
Bluetooth service discovery -> negotiated generation
                                      |
                      V1Protocol or V2Protocol
                                      |
           shared DeviceCapabilities + DeviceState
                                      |
                       HeadphonesController/QML
```

Dispatch keys should include generation, frame table, command and inquiry type.
Each adapter owns handshake, capability/state decoding, serializers and read-back
queries. The state layer owns intent revisions and session reset. A capability
snapshot should carry supported options, ranges, availability and whether a value
has been reported; a boolean supported flag is insufficient.

Recommended sequence:

1. Add service discovery to the connector and record the selected service in
   diagnostics. Keep Bluetooth names for display, and a clearly labeled manual
   fallback for devices whose service discovery fails.
2. Extract the existing V1 behavior behind an adapter with byte-for-byte regression
   checks against XM4 fixtures. Add V1 ANC/ambient capability handling and XM3 capture.
3. Add V2 handshake, identity, supported functions and capability parsing, initially
   exposing read-only information. Never reuse the V1 initialization sequence as a speculative
   fallback on an established V2 service.
4. Implement V2 battery, ANC/ambient, EQ and their notifications/read-back first.
5. Add connection preference, DSEE, voice guidance, power, touch/wearing and
   assignments only for advertised variants. Implement multipoint alerts and
   checking settings after reconnect as a separate step.
6. Generalize EQ UI from fixed five frequency labels plus Clear Bass to a capability
   descriptor. Preserve the existing editor when its layout matches; leave unknown
   layouts read-only until frequency/order/range meaning is established.
7. Validate XM5 and XM6 separately, including firmware identity, malformed replies,
   delayed notifications, writes followed by read-back, disconnects and reconnects.
   Custom-slot persistence requires a separate authorized persistence test.

Start with service discovery and reading V2 settings, then add controls as their
message formats are implemented.

## Research checklist and remaining gaps

| Question | Result |
| --- | --- |
| How should a protocol generation be chosen? | Answered in static code: advertised service, V2-first provider order; model names are not the deciding input |
| Can framing/session/UI be shared? | Answered structurally; generation-specific codecs are still required |
| What is the V2 startup sequence? | Recovered main sequence and protocol reply layout; complete compatibility/error policy remains to port |
| What prevents direct V1 command reuse? | Answered with function-record, EQ, ANC, power and system examples |
| What should change for broader V1/XM3 support? | Capability-driven ANC/ambient plus existing per-feature gates; actual XM3 capture remains absent |
| Which features/layouts does each XM5/XM6 firmware expose? | Needs captures from each model and firmware |
| Are V2 multipoint alerts, touch slots and wearing control fully decoded? | Partial: families and distinctions found; complete semantics/state machines remain open |
| Is custom EQ persistent across restart? | Not established for these models |

## Implementation status (2026-11-02)

Implemented:

- Windows cached enabled-service discovery selects V2 before V1 and records whether
  service discovery or the model-name fallback selected the protocol. This uses
  BluetoothEnumerateInstalledServices, not a complete live SDP inventory.
- V1 ANC capability parsing validates type and ambient ranges, including optional
  voice support. Unrecognised NC types disable the control. XM3 tests use scripted replies.
- Separate V2 dispatch/startup with the recovered protocol-version allowlist,
  capability identity, model/firmware, table-scoped functions and battery.
  Malformed/unsupported startup cannot fall back to V1 writes.
- V2 preset/custom EQ, availability, state and band metadata. Controls use the
  device's band count, ordering, frequency labels and step range. Unknown metadata
  and even step-count encodings remain read-only. Ten-band coverage is synthetic.
- ANC/ambient variants 13/14/16/17/19, capability ranges, availability and read-back.
  Extended mode fields are preserved. Aural waits for the final setting after adjustment.
- AUDIO connection preference 00 and DSEE auto/off 01, availability and read-back;
  COMMON codec and DSEE effect indicators are kept separate from configured mode.
  Selector00 has no capability-query subtype; its function declares its choices.
- Dynamic V2 dashboard, refreshed values, diagnostics/export and a read-only V2
  probe mode. All writes recheck capability/availability at send time. ACK and
  device-confirmed state remain distinct.

Still to implement: V2 voice guidance, power, touch/wearing, assignments,
multipoint alerts/reconnect state machine, custom-slot persistence, SHOT/LARGE_DATA
support. XM3, XM5 and XM6 also need hardware testing.

Additional audio evidence is retained in the REA lab's task-v2audio-findings.md:
`ev_eebb2a4f6691c5e24061e963dedeaaa46fe6dead305881c03a9144d5e20578ba`
(COMMON/function IDs) and
`ev_cbaebf6daa68263e4b325fe3f06429f8431fb4313988290bdf46078d6ab39465`
(availability/state initialization and capability validation).


The implementation has automated tests for startup, decoding, settings and error
handling. XM3, XM5 and XM6 hardware testing is still pending.
