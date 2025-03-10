# Automatic plant watering IoT project (Arduino)
## Description
This project consists of automatically watering a plant by checking the soil moisture level at certain time intervals.\
It also features a simple web interface to display information about temperature, air humidity, soil moisture level, and time since last watering. We can also set the interval at which the soil is checked.

## Technical
The program runs on an **ESP8266 NodeMCU** board and uses the **DHT11** temperature and humidity sensor, as well as a **resistive soil moisture sensor** with a comparator.\
The **5V water pump** is powered by the HW131 breadboard power supply. The ESP is powered by a 2200 mAh battery via the USB port.\
Pump power control is done with a transistor and a 2K resistor.
