# Protocol

> [!WARNING]
> Written from EVBox's document and community findings; **not verified on a real
> charger**. See the disclaimer in the [README](../README.md#disclaimer).

This component implements the third-party interface from EVBox *Protocol Max v4 –
Third party interface* (versions 1.0–4.0, 2016 onwards). The document describes an
RS-485 bus on G3 and G4 charging stations that is "intended to be used by third party
devices, allowing these devices to control the stations or smart grids maximum
available current real-time". It is not included here; a copy is linked under
[References](#references).

This is **not** the internal bus between the ChargePoint (modem) and the ChargeBox
(charger board). That bus uses the same framing, but different commands, and is
documented by [geekabit](https://www.geekabit.nl/projects/managed-ev-charger-to-stand-alone/protocol/).

## Topology

```text
Third-party device (SmartGrid module, 0xA0)
        │  third-party RS-485 bus
ChargePoint module (0x80)  ── cellular ──  back office
        │  station bus
ChargeBox 1 … ChargeBox n (max 20)
        │
      meter
```

## Transport

- RS-485, 38400 baud, 8 data bits, no parity, 1 stop bit.
- Multi-master: every module may transmit. Before transmitting, a module must see the
  bus idle for at least **100 ms**. Collisions are not detected; a corrupted frame is
  simply ignored.

## Frame

| Part | Size | Content |
|---|---|---|
| Start | 1 byte | `0x02` (STX) |
| Destination | 2 chars | hex, e.g. `80` |
| Source | 2 chars | hex, e.g. `A0` |
| Command | 2 chars | hex, e.g. `69` |
| Data | variable | hex, words are 4 characters |
| Checksum 1 | 2 chars | sum of all characters of destination, source, command and data, modulo 256 |
| Checksum 2 | 2 chars | XOR of the same characters |
| End | 1 byte | `0x03` (ETX) |

Everything between STX and ETX is uppercase ASCII. Both checksums are calculated over
the ASCII characters, not over the decoded bytes. A frame with a wrong checksum is
ignored. (On the internal station bus the frame ends in `0x03 0xFF`; the
specification defines a single `0x03`.)

Addresses: ChargePoint `0x80`, SmartGrid module `0xA0`, broadcast `0xBC`.

Worked example from the specification, verified with the algorithm above:

```text
body     80A06900E6008C0154003C002800500046
sum      0xF7
xor      0x03
frame    <STX>80A06900E6008C0154003C002800500046F703<ETX>
```

The two other examples in the specification carry checksums that do not match their
content; they appear to have been edited without recalculating.

## Command 69: maximum phase currents

### Request (SmartGrid module → ChargePoint)

| Field | Size | Unit |
|---|---|---|
| Maximum current L1 | word | 0.1 A |
| Maximum current L2 | word | 0.1 A |
| Maximum current L3 | word | 0.1 A |
| Timeout | word | s |
| Maximum current L1 after timeout | word | 0.1 A |
| Maximum current L2 after timeout | word | 0.1 A |
| Maximum current L3 after timeout | word | 0.1 A |

Example as sent by this component, 16.0 A per phase, 60 s timeout, 10.0 A fallback:

```text
<STX>80A06900A000A000A0003C006400640064EF75<ETX>
```

### Reply (ChargePoint → SmartGrid module)

| Field | Size | Unit |
|---|---|---|
| Minimum allowed interval | word | s |
| Maximum phase current of the connection | word | 0.1 A |
| Number of ChargeBox modules *n* | byte | |
| For each ChargeBox: minimum required phase current | word | 0.1 A |
| … used current L1, L2, L3 | 3 words | 0.1 A |
| … cos φ L1, L2, L3 | 3 words | 0.001, signed (`FFBF` = −0.065) |
| … meter reading | 2 words | Wh |

A reply for one ChargeBox has 46 data characters. Example from the specification (the
data of the second ChargeBox omitted):

```text
A08069 0014 015E 02 | 0078 008C 008C 008C 021C 0140 0384 00014C1D | …
       │    │    │    │    │              │              └ 85021 Wh
       │    │    │    │    │              └ cos φ 0.540 / 0.320 / 0.900
       │    │    │    │    └ 14.0 A on each phase
       │    │    │    └ minimum 12.0 A
       │    │    └ 2 ChargeBoxes
       │    └ connection maximum 35.0 A
       └ frames at most every 20 s
```

### Behaviour

- Initially, the maximum currents equal the connection's rating. A command can lower
  them but never raise them above that rating.
- The message is a keep-alive: it must be repeated within the timeout. If the
  ChargePoint receives nothing before the timeout, the maximum currents become the
  "after timeout" values.
- It can take up to 10 seconds before the car follows the new currents.
- Do not send more often than the minimum allowed interval from the reply.

## Command 68

Same as command 69, with the ChargePoint's serial number (4 bytes) and 16 bytes of
free "information module" data added in front of the currents. A ChargePoint only acts
on it when the serial matches or is `0`, so several ChargePoints can share one bus. Its
reply also includes the voltage per phase for each ChargeBox. This component does not
use it.

## References

- EVBox, *Protocol Max v4 – Third party interface*:
  [PDF on forum.iobroker.net](https://forum.iobroker.net/assets/uploads/files/1619202515030-protocol-max-v4.pdf)
- Maarten Tromp, [EVBox protocol documentation](https://www.geekabit.nl/projects/managed-ev-charger-to-stand-alone/protocol/)
  (internal station bus)
- Maarten Tromp, [EVBox hardware documentation](https://www.geekabit.nl/projects/managed-ev-charger-to-stand-alone/hardware/)
