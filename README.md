# ESP32 Camera with GPS

## Use Case

A mobile ESP32 Camera with GPS Receiver. The camera image can be accessed as snapshot and stream via webserver and uploaded in regular intervals to a MQTT broker and to a nextcloud directory.

## Hardware

### OV5640 ESP32 CAM

A ESP32 Module from [Aliexpress](https://de.aliexpress.com/item/1005004227739159.html?gatewayAdapt=glo2deu) with a OV5640 5 MP sensor.

### Ublox GY-NEO6MV2 GPS Modul NEO-6M

## Software

## TODO

- [X] Base System
- [X] WiFi Client
- [ ] Fallback: WiFi Access Point
- [X] HTTP Server
- [X] Cam: Snapshot
- [X] Cam: Stream
- [X] MQTT Connection
- [ ] MQTT: Publish fw info
- [ ] MQTT: Publish GPS data
- [ ] MQTT: Cyclic image publish
- [ ] MQTT: Image on request
- [ ] GPS: Connectivity
- [ ] GPS: Data parsing
- [ ] GPS: Add time and position to cam image
