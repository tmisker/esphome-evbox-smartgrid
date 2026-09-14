# Installation

> [!CAUTION]
> An EV charger carries mains voltage. Opening it and connecting anything inside is
> work for a qualified electrician. Switch off the charger's circuit breaker and
> verify that it is dead before opening the enclosure. Leave the seals of the kWh
> meter intact.

> [!WARNING]
> **Untested. At your own risk.** Nothing in this guide has been tried on a real
> charger. The location and pinout of the third-party bus are inferred from public
> documentation and photos of other units, and may be wrong for yours. A wrong
> connection can damage the charger. Verify every step yourself, and stop as soon as
> anything does not match what you find. See the disclaimer in the
> [README](../README.md#disclaimer).

## 1. Check the charger generation

G3 chargers have two modules: a charger board (*ChargeBox*, 471011.x) and, plugged onto
it, a modem board (*ChargePoint*). Signs of G3:

- The box serial number on the label starts with `B`.
- The OCPP configuration key `evb_BootInfo`, if your backend shows it, contains a
  model such as `G3-M5320E`.
- Modem boards seen on G3: 471003 (SIM908 or SIM928A), 471046.x (UMTS-E, SIM5320E)
  and 471049-S (4G, SIM7500E).

G4 chargers combine both modules on one board and are not covered here.

## 2. Find the third-party bus

Protocol Max v4 places the third-party bus on the ChargePoint, separate from the
station bus to the ChargeBoxes. The
[geekabit hardware documentation](https://www.geekabit.nl/projects/managed-ev-charger-to-stand-alone/hardware/)
notes that the G3 modem has "a second RS-485 bus, available on the green connector on
the side".

On photos of 471046.2 and 471046.3 modem boards there are two green connectors:

| Connector | Where | What |
|---|---|---|
| Small 4-pole Phoenix Contact MC 1,5 (3.81 mm pitch), two protection diodes next to it | top edge of the modem | **third-party bus (expected)** |
| Larger 4-pole connector, 5.08 mm pitch | side of the modem | link to the ChargeBox: station bus and 12 V supply |

Do **not** connect to:

| Location | What |
|---|---|
| ChargeBox terminals 1–2 | RS-485 bus of the kWh meter |
| ChargeBox terminals 28–29 (black) and 34–35 (green) | station bus between ChargePoint and ChargeBoxes |
| The larger connector between modem and ChargeBox | station bus and supply |

## 3. Identify the pins

The pinout of the small connector is not documented.

With the power **off**:

1. Measure continuity from each pin to GND of the modem's larger connector. On the
   ChargeBox side this is terminal 33 (`0V / DC`); geekabit lists it as pin 1 of the
   4-pin header.
2. Measure continuity to its +12 V (terminal 36, `12V / DC`; pin 4 of the header).
3. The two remaining pins are RS-485 A and B.

With the power **on**, on the low-voltage side only: confirm the voltage between GND
and the 12 V pin, if there is one.

A and B are labelled inconsistently in EVBox documentation. If communication fails,
swap them; this does no harm.

## 4. Connect the ESP32

For the M5Stack AtomS3 Lite with ATOMIC RS485 Base:

| Bus | ATOMIC RS485 Base |
|---|---|
| A | A |
| B | B |
| GND | GND |
| +12 V, if present | power input (6–24 V) |

- If the small connector carries no 12 V, feed the ESP32 from another supply and
  connect its ground to the bus GND.
- Use a short twisted pair. A cable of a few tens of centimetres needs no termination.
- Check Wi-Fi reception at the charger before closing the enclosure.

## 5. Commission

1. Flash [example/evbox-smartgrid.yaml](../example/evbox-smartgrid.yaml) with
   `max_current: 16`, the charger's own maximum. This changes nothing.
2. The log shows `Sent 16.0 A per phase …` at every update.
3. **ChargePoint responding** turns on and the sensors fill. If not, swap A and B and
   check GND.
4. If your OCPP backend shows configuration keys, `evb_LastSmartGridModuleMessage`
   should now contain a recent value.
5. While charging, set the limit to 10 A. Within about 10 seconds the car should draw
   about 10 A per phase. Set it back to 16 A.
6. Test the fail-safe: switch off **SmartGrid keep-alive**, or disconnect the ESP32.
   After the timeout the charger should apply `fallback_current`.
7. Test pausing: set the limit below the charger's minimum, for example 0 A. Check
   that charging pauses, and resumes when the limit goes back up.

## Troubleshooting

| Symptom | Check |
|---|---|
| No reply, no bus activity in the log | A and B swapped, missing GND, wrong connector |
| `Ignoring frame with invalid checksum` | baud rate, ground, interference; a few are harmless |
| Replies arrive but the car does not follow | wait 10 s; check `connection_max_current` and `minimum_current` |
| Current returns to the fallback on its own | timeout shorter than the actual frame spacing; see `minimum_interval` |
