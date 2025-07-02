# ESP32-CAN-GPS-to-MQTT-live-map

This project is a VERY early WIP and also largely a personal project I intend to share as-is.

The current idea and working setup is having an ESP32 compatible board set up to retrieve and decode GPS data over the CAN bus in a (modern) car, then send that to an MQTT broker for use elsewhere. A basic Google Maps based web page is provided that will take this GPS data from the MQTT broker and display a real time location of the vehicle including bonus features such as showing an acccurate heading/direction, auto centering, and a bonus weather radar layer on the map. Additional data points from the vehicle such as number of sats, speed, VDOP/HDOP values, and actual vs inferred status (if available) are currently displayed on screen for now in its WIP state.

More to come and this README will very likely receive significant updates in the coming days!

## regen-braking branch

Additional side work trying to pull and display regenerative braking data for the Ford C-Max (and maybe other Ford EVs/PHEVs). Goal is to have a threshold/limit display that the car doesn't currently offer natively. WIP
