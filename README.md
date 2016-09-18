# Solarbit Mining Module - Arduino/Genuino

Well... it's alive!

The impressive 1.5 KH/sec on my Arduino 101 means that we only need another thousand trillion of these fellas running to make a mining profit.

The `solarbit` SMM now uses the WiFi101 shield instead of being tethered to the USB. Another small step towards plugging it into a solar panel..

I suspect these sketches wouldn't be hard to port to an Uno and the original WiFi shield?

The `solarbit_setup` sketch checks your hardware setup and adds configuration to EEPROM for the SMM for the WiFi and Pool connections.

Now taking a first stab at the [Protocol](./protocol.md)

Anyone know where to get ASICs? Anyone know how to make a shield for them?

## Notes

I had to use a pull request from Martino Facchin to get EEPROM to work on the 101 board, which you can find in https://github.com/01org/corelibs-arduino101/pull/281 (This should make it into the Curie 1.0.7 release, I hope).

As you'll see, I used Brad Conte's SHA256. I didn't fork his repo as the changes I needed were already making the code too specific to this project and there's no point pushing anything back. No way we're not going to use ASIC in any case... You can find his code at https://github.com/B-Con/crypto-algorithms
