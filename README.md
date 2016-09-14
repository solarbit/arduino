# Solarbit Mining Module - Arduino/Genuino

Well... it's alive!

The impressive 1.5 KH/sec on my Arduino 101 means that we only need another thousand trillion of these fellas running to make a mining profit.

I'm currently updating the `solarbit` SMM to use the WiFi101 shield instead of being tethered to the USB.

The `solarbit_setup` sketch will be used to check your hardware setup and add configuration to EEPROM for the next update to the SMM.

Anyone know where to get ASICs? Anyone know how to make a shield for them?

## Notes

I had to use a pull request from Martino Facchin to get EEPROM to work on the 101 board, which you can find in https://github.com/01org/corelibs-arduino101/pull/281 (This should make it into the Curie 1.0.7 release).

As you'll see, I used Brad Conte's SHA256. I didn't fork his repo as the changes I needed were already making the code too specific to this project and there's no point pushing anything back. No way we're not going to use ASIC in any case... You can find his code at https://github.com/B-Con/crypto-algorithms
