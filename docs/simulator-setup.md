# Simulator network setup

Use these instructions before running the [Wi-Fi test firmware](../examples/fbw_external_power/README.md).

## MSFS 2024 PC

1. Find the configuration location for your edition using Microsoft's
   [MSFS 2024 SimConnect XML documentation](https://docs.flightsimulator.com/msfs2024/html/6_Programming_APIs/SimConnect/SimConnect_XML_Definition.htm).
   Store and Steam installations can differ; do not assume an MSFS 2020 path.
2. Back up an existing `SimConnect.xml`. Add a TCP/IPv4 communication entry
   without discarding existing clients' entries.
3. Choose a fixed port, for example 5111. Bind to the simulator PC's LAN address
   or use the SDK-supported wildcard address.
4. Permit inbound TCP on that port in Windows Firewall for the private network,
   restricted to the ESP32 address or trusted LAN subnet.
5. Restart the simulator after changing configuration.

Example communication entry to merge into the SDK's document structure:

```xml
<SimConnect.Comm>
    <Protocol>IPv4</Protocol>
    <Scope>local</Scope>
    <Port>5111</Port>
    <MaxClients>64</MaxClients>
    <MaxRecvSize>41088</MaxRecvSize>
    <Address>0.0.0.0</Address>
</SimConnect.Comm>
```

`Scope` here follows the upstream network configuration example; it is not a
firewall rule. Wildcard binding exposes the listener on multiple interfaces.
Prefer a LAN-only binding where practical. Confirm settings against the SDK
version installed with your simulator.

## ESP32 side

- Use a 2.4 GHz Wi-Fi network supported by ESP32-S3.
- Use the **PC's LAN IP**, not `localhost`, as the simulator host.
- The configured TCP port must match the XML entry.
- Both devices must have a route to each other. Guest Wi-Fi/client isolation
  can prevent access even when both devices have Internet access.
- Reserve the PC's DHCP address to avoid host changes.
- Keep Wi-Fi credentials out of Git and logs.

Treat SimConnect as a trusted-LAN interface. Do not expose it with router port
forwarding or publish it directly on the Internet.

## Troubleshooting order

1. Verify ESP32 association and DHCP address.
2. Verify the simulator PC address and listening TCP port.
3. Check XML location, Windows Firewall, and Wi-Fi client isolation.
4. Distinguish TCP connection from a successful SimConnect OPEN handshake.
5. Inspect simulator exceptions and the SimConnect Inspector before blaming
   aircraft variables.

References:
[upstream network guide](https://github.com/EvenAR/node-simconnect#running-over-network),
[SDK API reference](https://docs.flightsimulator.com/msfs2024/html/6_Programming_APIs/SimConnect/SimConnect_API_Reference.htm).
