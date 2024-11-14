# power-monitor
solar panel and grid power monitor made with ESP32 Arduino WIFI 2.8 "240*320 TFT LCD Display Touch ESP-WROOM-32
this project is a development of original project made by MAURIZIO GIUNTINI 
https://mauriziogiunti.it/un-power-monitor-per-l-impianto-fotovoltaico-con-arduino
https://github.com/giuntim/arduino-power-monitor
![POWER MONITOR](https://github.com/user-attachments/assets/012bbacb-fbfd-4e4b-b0d4-8f3b644f5bb6)

display info:
FV: active power generation from solar panels
Day max: is the daily max active power from solar panels (FV)
Max: is the max power measured from FV
% value and progressbar: is the percentage of actual power from FP over the installed FV power
(negative sign for energy and power generated by FV)
day gen FV: is the daily energy generatd by FV

Rete: active power adsobed from the Grid
Day max: is the daily max active power from Grid
Max: is the max power measured from Grid
% value and progressbar: is the percentage of actual power from the Grid over the contractual maximum power 
(negative sign for energy and power injected on the Grid - excess of generation)
Day ass rete: is the daily energy adsorbed from the Grid

in case of excess of generation the background screen will be RED

Shelly EM2 connected as indicated on his installation manual
