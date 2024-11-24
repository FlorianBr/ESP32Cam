# ESP32 Camera with GPS

## Use Case

A mobile ESP32 Camera with GPS Receiver. The camera image can be accessed as snapshot and stream via webserver and uploaded in regular intervals to a MQTT broker and to a nextcloud directory.

## Hardware

### OV5640 ESP32 CAM

### Ublox GY-NEO6MV2 GPS Modul NEO-6M

## Software

## TODO

- [ ] Base System
- [ ] WiFi Client
- [ ] Fallback: WiFi Access Point
- [ ] HTTP Server
- [ ] Cam: Snapshot
- [ ] Cam: Stream
- [ ] MQTT Connection
- [ ] MQTT: Cyclic image publish
- [ ] MQTT: Image on request
- [ ] GPS: Connectivity
- [ ] GPS: Data parsing
- [ ] GPS: Add time and position to cam image
