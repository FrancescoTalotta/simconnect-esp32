# Example: read a variable and drive an LED

Firmware 0.1.3 mirrors the Korry ON legend, superseding 0.1.2's selection-only
demonstration alongside BOOT/external power:

```text
FlyByWire PROBE/WINDOW HEAT Korry ON-legend inputs
  -> SimConnect LVar response
  -> validate and decode four FLOAT64 values in one snapshot
  -> publish latest reading to an output task
  -> onboard RGB LED blue or off
```

The [complete application](../examples/fbw_external_power/main/app_main.c) contains
the following steps. Aircraft checks, errors, and loading behavior are part of the
full implementation, not omitted from the runnable example.

## 1. Choose the variable

```c
static const simc_item_t probe_heat_items[] = {
    { "L:A32NX_MAN_PITOT_HEAT", "number", SIMC_FLOAT64, 0 },
    { "L:A32NX_ELEC_AC_2_BUS_IS_POWERED", "number", SIMC_FLOAT64, 0 },
    { "L:A32NX_ELEC_DC_2_BUS_IS_POWERED", "number", SIMC_FLOAT64, 0 },
    { "L:A32NX_OVHD_INTLT_ANN", "number", SIMC_FLOAT64, 0 },
};
```

[FlyByWire's API](https://docs.flybywiresim.com/aircraft/a32nx/a32nx-api/a32nx-flightdeck-api)
documents this as the manual PROBE/WINDOW HEAT function/button selection.
In this example, zero means AUTO; 1 or -1 means manual ON.

**It does not mean actual heating is off in AUTO.** Windows and probes can heat
automatically with an engine running or in flight. The LED mirrors the
ON legend, not heating power, circuit health, or every heater's actual state.
The installed aircraft release still needs comparison with the reference below.

### Source-traced lamp status (v2024.1.0)

There is no separate lamp-status variable in the traced template: the cockpit
computes its emissive from these inputs:

```text
lit = AC2_powered AND (manual_ON OR (ANN == TEST AND DC2_powered))
level = lit ? (ANN == DIM ? 0.1 : 1.0) : 0
```

`ANN` is 0=TEST, 1=BRT, 2=DIM. AC2 is required even during lamp test.
Do not substitute `PITOT HEAT`: automatic heating is not the Korry ON legend.

Pinned upstream sources, release **v2024.1.0**, commit
`161051add8a6959e3325289dbebe097c0793056f`:

- [Heat template: SEQ2_CODE uses manual selection](https://github.com/flybywiresim/aircraft/blob/161051add8a6959e3325289dbebe097c0793056f/fbw-a32nx/src/behavior/src/A32NX_Interior_Misc.xml#L253-L268).
- [Generic Korry template: test override and dimming](https://github.com/flybywiresim/aircraft/blob/161051add8a6959e3325289dbebe097c0793056f/fbw-a32nx/src/behavior/src/A32NX_Interior_Generics.xml#L185-L324).
- [Cockpit instance: AC2 power gate](https://github.com/flybywiresim/aircraft/blob/161051add8a6959e3325289dbebe097c0793056f/fbw-a32nx/src/base/flybywire-aircraft-a320-neo/SimObjects/AirPlanes/FlyByWire_A320_NEO/model/A320_NEO_INTERIOR.xml#L3264-L3275).

This reproduces the source's logical emissive level, not measured rendered
luminance. The physical PWM DIM level rounds to the nearest step (16 becomes 2),
with a minimum of 1 for a nonzero configured brightness. Zero disables output.

## 2. Register and subscribe

The session owner calls these operations and checks their results:

```c
simc_add_definition(session, HEAT_DEF, probe_heat_items, 4);
simc_request_data(session, HEAT_REQUEST, HEAT_DEF, SIMC_PERIOD_SECOND, 0);
```

The example uses definition ID 3 and request ID 2. Request 1 is the aircraft/power
snapshot; one-shot action IDs start at 3 so these requests cannot collide.
Definition IDs and request IDs are separate namespaces.

This requests one reading every second, including unchanged values. Using only
CHANGED would make a steady ON value appear stale unless a separate heartbeat
were added.

## 3. Decode the matching response

The existing polling loop routes only matching definition/request IDs to
`handle_probe_heat()`. Once the flight/aircraft gate is ready:

```c
simc_value_t values[4];
int result = simc_decode_data(message, probe_heat_items, 4, values);
```

The handler also checks the actual aircraft object ID and validates the boolean
values and the ANN enum. All four inputs arrive in a single 32-byte snapshot;
invalid or incomplete responses switch the output off. A successful reading sets
the sample's selection, power and ANN fields and
publishes it using `xQueueOverwrite(probe_samples, &sample)`.

No simulator write is made for probe heat. Changing this cockpit button with
the mouse demonstrates simulator-to-hardware communication. BOOT remains the
separate hardware-to-simulator external-power example.

## 4. Trigger the physical action

`indicator_task()` owns the LED. It calls:

```c
board_led_set_blue(&rgb_led, blue, brightness);
```

Only READY and CONFIRMING permit blue. Loading, mismatched/invalid data, a new
connection, or a sample older than five seconds forces off. A one-slot queue
keeps only the latest value; connection and invalidation epochs prevent cached
ON data from reappearing after a quick disconnect or aircraft change.

The output task checks about every 20 ms independently of networking. Successful
cockpit changes normally appear within the one-second sampling period.
`status` reports `probe_led=BLUE` or `off`; this is the commanded LED state,
not optical feedback.

## 5. RGB hardware

The reference DevKitC-1 **v1.1** has a SK68XXMINI-HS addressable RGB LED on
**GPIO38**. The always-on power LED is not this LED and is not software-controlled.

- `APP_RGB_LED_GPIO`: default 38; use 48 only for the documented original v1.0.
- The user's physical board was tested successfully on **GPIO48**; its ignored
  configuration overrides the v1.1 reference default. Do not infer board revision
  from the module marking alone.
- `APP_RGB_LED_BRIGHTNESS`: default 16/255 for BRT/TEST; DIM uses 10%;
  zero disables the output.
- Backend: built-in ESP-IDF RMT TX/copy encoder, no external component.
- Frame: 24 GRB bits, MSB first; one 300 microsecond low latch.
- Clock: 10 MHz; zero is 3 high/9 low ticks, one is 9 high/3 low ticks.
- A persistent 25-symbol buffer is held unchanged until transmission completes.
- Initialization sends OFF before networking/tasks start. RMT errors fail loudly
  rather than reusing an in-flight buffer.

See [board driver](../examples/fbw_external_power/main/board_led.c),
[output policy](../examples/fbw_external_power/main/probe_indicator.h),
the [Espressif board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html),
and [SK68XXMINI-HS timing/GRB specification](https://www.normandled.com/upload/202105/SK6812MINI-HS%20LED%20Datasheet.pdf).

## Test on the simulator

1. Flash the new example using the [existing instructions](../examples/fbw_external_power/README.md).
2. Wait for READY, AC BUS 2 powered, ANN=BRT; select PROBE/WINDOW HEAT ON.
3. Expect `RGB LED=BLUE (heat ON legend)` and a blue LED.
4. Return the pushbutton to AUTO; expect OFF, even if automatic heating is active.
5. Leave ON unchanged for more than five seconds: it should stay blue because
   periodic readings refresh its age.
6. Load another aircraft or interrupt Wi-Fi: it should switch off and remain off
   until the expected aircraft and a fresh heat value are verified.
7. Verify BOOT still toggles external power.
8. With AUTO and AC2/DC2 powered, hold ANN LT TEST: both the cockpit ON legend
   and RGB LED should illuminate. Release to BRT: both should extinguish.
9. With manual ON, switch BRT/DIM: both should dim (physical LED 16 -> 2 by default).
10. Remove AC2 power: the LED should go off regardless of selection or test.
    With AC2 powered but DC2 unpowered, test must not override AUTO.

Host tests cover subscription/decoding, policy invalidation/staleness, and GRB bit
order/timing values, plus every selection/power/test combination. GPIO48 blue
output was confirmed by the user. Lamp-test, power and dimming behavior on the
installed aircraft still require validation; its exact release is not recorded.
