# Reading and writing LVars

Numeric LVars can be accessed directly through SimConnect on supported MSFS
versions. A WASM bridge is not inherently required for this operation.

Use the full `L:` name, units `number`, and FLOAT64:

```text
Name:  L:A32NX_OVHD_ELEC_EXT_PWR_PB_IS_ON
Units: number
Type:  FLOAT64
Value: 0 = off, 1 = on
```

Use these parameters with the [C API](api.md); the complete
[example firmware](../examples/fbw_external_power/main/app_main.c) is included.

## Read

1. Load the intended aircraft and allow its systems to initialize.
2. Add the LVar to a SimConnect data definition.
3. Request ONCE or subscribe at an appropriate recurring period.
4. Match and validate the response before decoding the FLOAT64.

## Write

1. Confirm that the aircraft documents the LVar as writable.
2. Define it with the same name, units, and datatype.
3. Send `SetDataOnSimObject` with the desired numeric value.
4. Observe subsequent state updates rather than treating socket success as an
   acknowledgement.

Changing an internal LVar can bypass aircraft logic. Prefer the aircraft's
documented control interface. A returned zero alone does not prove that the
correct aircraft or a meaningful variable is present; local variables may be
created on first reference. Do not use a probe of an arbitrary LVar as an
aircraft identity test.

## External-power example

FlyByWire documents:

| Purpose | SimConnect name | Access |
| --- | --- | --- |
| Cockpit EXT PWR selection | `L:A32NX_OVHD_ELEC_EXT_PWR_PB_IS_ON` | Read/write |
| External power availability | `L:A32NX_EXT_PWR_AVAIL:1` | Read |

Use numeric units for both. Confirm the indexed availability name in the
installed release before relying on it. Do not write availability to manufacture
a connected GPU.

The build and test workflow is described in the
[example instructions](../examples/fbw_external_power/README.md).

For a read-only LVar that drives an ESP32 output, see
[PROBE/WINDOW HEAT → RGB LED](read-to-led.md).

References:
[SDK LVar support](https://docs.flightsimulator.com/html/Programming_Tools/SimConnect/API_Reference/Events_And_Data/SimConnect_AddToDataDefinition.htm),
[FlyByWire Flight Deck API](https://docs.flybywiresim.com/aircraft/a32nx/a32nx-api/a32nx-flightdeck-api).
