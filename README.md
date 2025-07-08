# WiFi Control for Mitsubishi (MHI) RC-EX3 Air Conditioner

This project adds MQTT and HTTP control to the Mitsubishi RC-EX3 wall controller using an ESP-12 (ESP8266). It is based on the excellent work from [mcchas/rc-ex3-esp](https://github.com/mcchas/rc-ex3-esp). Many thanks to the original author for providing the foundation.

## Building the hardware

Build and flash the firmware to the ESP module. Connect VCC, GND, TX and RX to the panel by inserting bent jumper pins into the through-hole vias. Power the ESP-12 with a 3.3&nbsp;V regulator.

[<img src="images/rc3-overview.png" width=50%/>](image.png)

Solder ground and positive leads to the regulator.

[<img src="images/buck.png" width=50%/>](image.png)

Place the regulator inside the enclosure and press the jumper pins into the vias. The ESP-12 can sit flat against the PCB once insulated.

[<img src="images/rc3-regulator-placement.png" width=50%/>](image.png)

[<img src="images/rc3-ttl-uart.png" width=50%/>](image.png)

[<img src="images/rc3-regulator-power.png" width=50%/>](image.png)

## Updating firmware

After building, update using `esptool.py`:

`python3 espota.py --ip=<ESP8266 IP Address> --host_ip=0.0.0.0 --port=8266 --host_port=8267 --file=./.pio/build/d1_mini/firmware.bin --debug --progres`

## Using the original software

The device exposes a TCP socket on port 1123 which works with the Mitsubishi PC-Remote tool (PC-RemoteSetup.exe). This can help reverse additional functions.

## Setup

When uninitialised the device advertises a WiFi Manager AP. Use it to set your network credentials, MQTT topic and server details (`http://192.168.4.1`).

Fetch the unit state with the status command and set the state with a JSON payload:

```
{
    "power": true/false,
    "mode": cool/dry/heat/fan/auto
    "speed": 0/1/2/3/4
    "temp": 16.0-30.0
    "delayOffHours": 1-12
}
```

## Home Assistant

The firmware announces itself via MQTT discovery so it will appear automatically as a climate entity when the MQTT integration is enabled. Configure the broker settings in `config-private.h`.
Discovery uses the full MQTT property names as expected by recent Home Assistant releases (2025.7+).

Each attribute is published under `<BASE_TOPIC>/<item>`. With the default `BASE_TOPIC` of `mhi-ac-rc-ex3-1` the following topics are used:

- `mhi-ac-rc-ex3-1/power/state` (`.../set` to change)
- `mhi-ac-rc-ex3-1/mode/state`
- `mhi-ac-rc-ex3-1/temp/state`
- `mhi-ac-rc-ex3-1/speed/state`

Updates from the wall controller are pushed over MQTT so Home Assistant always reflects the latest state.
