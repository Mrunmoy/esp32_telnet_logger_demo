# esp32_telnet_logger_demo
A task error/msg log using Telnet on ESP32 (demo project)

configure:
```make menuconfig```
Then setup the SSID id and password

run:
```make flash monitor```


test: 
lookup the ip address of the esp32 printed on 'monitor' window above on a separate commmandline
```telnet ipaddr 65056```


Once you are connected, you will start reciving log msgs from two sample tasks running on the esp32
