# trannergy2pvoutput_v2
Upload your Trannergy statistics to PVoutput.org

Read your Trannergy Solar power Inverter over WIFI. Processes data and sends it to PVoutput.org You will need an API from PVoutput.org if you want it to upload.
For ESP8266. Might work with ESP32, not tested!

This is Version2. Some code optimisation and better Telnet.

You can telnet this device

telnet {IPNUMBER}
e.g. telnet 192.168.1.1

(fill in you IP number)

press 'H' or 'h' for help:

R: Reset

C: Clear and stop

F: FreeHeap

B: Reboot report

V: Version

D: Toggle Debug

L: Latest output


It polls the Trannergy Inverter every minute (press 'L" if you cannot wait)
Every 5 minutes it uploads data to PVoutput.org

When using a linux OS (raspberrry e.g.) try https://github.com/arjenv/omnikstats

Rename your_secrets.h to secrets.h

Edit secrets.h. Fill in your SSID, password, api, ID, Ipnumber of your inverter etc.

Recommend to put your inverter's IP addres into a static addres since it will shut down every night. Otherwise you might get a new IPnumber via DHCP every morning and the ESP will not connect.

You can connect a temperature sensor to pin 14 to get surrounding temperature data.

This software is ported from omnikstats and not optimised for ESP8266.

Besides, I am not a software guy... :-)

Free to use and optimise. However, pls leave my credentials
