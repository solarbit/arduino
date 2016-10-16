// Solar Mining Module Prototype A
// https://www.solarbit.cc
// NOTE: Run solarbit_setup first to configure EEPROM parameters!

#include <EEPROM.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <CurieBLE.h>
#include <SolarBit_SMM.h>

typedef struct {
	boolean tethered;
	boolean paused;
	unsigned long boot_time;
} status_t;

status_t status;
config_t config;
packet_t packet;
report_t report;
WiFiUDP udp;

BLEPeripheral blePeripheral;
BLEService smmService("a3c1622d-2670-1298-f31b-49a1a9ba6776");
BLECharCharacteristic poolCharacteristic("fd0147e0-425e-a0a6-76ca-1977f08edd16", BLERead | BLEWrite);

void setup() {
	status.tethered = is_tethered();
	boolean success = load_configuration_from_eeprom();
	if (success) {
		success = connect_to_wifi();
	}
	if (success) {
		udp.begin(config.param.port);
		int status = SMM.status();
		println("SolarBit Mining Module Ready");
		print("SMM Shield Status: ");
		println(status);
		println();
		print("SMM Status:\n ");
		println(get_status_type(status));
		success = setup_bluetooth();
		if (success) {
			println("Bluetooth Enabled");
		}
		send_message(HELO);
		delay(500);
	}
	status.paused = false;
}



void loop() {
	blePeripheral.poll();
	if (WiFi.status() == WL_CONNECTED) {
		check_pool();
		int status = SMM.mine();
		if (status == SMM_DONE) {
			print("SMM Status:\n ");
			println(get_status_type(status));
			SMM.report(&report);
			send_message(INFO, report.bytes, sizeof(report_t));
		}
	} else {
		println("WiFi Disconnected");
		do {
			if (! connect_to_wifi()) {
				delay(60000);
			}
		} while (WiFi.status() != WL_CONNECTED);
		println("WiFi Reconnected");
	}
}


// TODO: Maybe a shield LED?
void do_indicator() {
	// SPI used for WiFi takes away LED pin 13
	return;
}


boolean load_configuration_from_eeprom() {
	for (unsigned int i = 0; i < sizeof(config_t); i++) {
		config.bytes[i] = EEPROM.read(i);
	}
	return is_configuration_valid();
}


boolean is_configuration_valid() {
	return memcmp((const uint8_t*)config.param.magic, MAGIC, 4) == 0
		&& memcmp((const uint8_t*)config.param.trailer, MAGIC, 4) == 0;
}


boolean setup_bluetooth() {
	blePeripheral.setAdvertisedServiceUuid(smmService.uuid());
	blePeripheral.addAttribute(smmService);
	blePeripheral.addAttribute(poolCharacteristic);
	poolCharacteristic.setValue(3);
	blePeripheral.setEventHandler(BLEConnected, blePeripheralConnectHandler);
	blePeripheral.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);
	poolCharacteristic.setEventHandler(BLEWritten, poolCharacteristicWritten);
	blePeripheral.setLocalName("SolarBit Miner");
	blePeripheral.setDeviceName("SolarBit Miner");
	return blePeripheral.begin();
}


boolean connect_to_wifi() {
	int status = WiFi.status();
	int tries = 3;
	while (status != WL_CONNECTED && tries > 0) {
		status = WiFi.begin((char *)config.param.ssid, (char *)config.param.password);
		if (status != WL_CONNECTED) {
			tries--;
			delay(5000);
		}
	}
	return (WiFi.status() == WL_CONNECTED);
}


boolean connect_to_mining_pool() {
	udp.begin(config.param.port);
	int tries = 3;
	boolean success = false;
	while (tries > 0 && !success) {
		send_message(HELO);
		tries--;
		delay(1000);
		success = receive_message();
	}
	return success > 0;
}


void check_messages() {
	int type = receive_message();
	switch (type) {
		case NONE:
			break;

		case PING:
			send_message(HELO);
			break;

		case SYNC:
			status.boot_time = packet.message.header.sync - (millis() / 1000);
			send_message(NODE, config.param.address, strlen((char *)config.param.address));
			break;

		case POOL: {
				uint8_t *coinbase = (uint8_t *)&packet.message.payload;
				uint8_t length = packet.message.header.payload_size;

				println("Received COINBASE from Pool");
				print_hex(coinbase, length);

				int status = SMM.begin(coinbase, length);
				if (status == SMM_READY) {
					send_message(OKAY);
				} else {
					send_message(NACK, (uint8_t *)&packet.message.header.sync, 4);
				}

				print("SMM Status:\n ");
				println(get_status_type(status));
			}
			break;

		case STAT:
			SMM.report(&report);
			send_message(INFO, report.bytes, sizeof(report_t));
			break;

		case MINE:
			{
				if (packet.message.header.payload_size == 0) {
					status.paused = false;
					return;
				}
				// Preserve payload
				packet_t data;
				memcpy(data.bytes, packet.bytes, sizeof(packet_t));

				// Send last best attempt
				report.value.tethered = status.tethered;
				report.value.paused = status.paused;

				SMM.report(&report);
				send_message(INFO, report.bytes, sizeof(report_t));

				print("For BLOCK ");
				print(report.value.height);
				print(" hashrate was ");
				println(report.value.hash_rate);

				int offset = sizeof(message_header_t);
				uint32_t *block_height = (uint32_t *)&data.bytes[offset];
				offset += sizeof(uint32_t);
				block_t *block = (block_t *)&data.bytes[offset];
				offset += sizeof(block_t);
				uint8_t *path_length = (uint8_t *)&data.bytes[offset];
				offset += sizeof(uint8_t);
				uint8_t *merkle_path_bytes = &data.bytes[offset];

				int smm_status = SMM.init(*block_height, block, *path_length, merkle_path_bytes);

				print("SMM Status:\n ");
				print(get_status_type(smm_status));
				print(" height=");
				println(*block_height);

				if (smm_status != SMM_MINING) {
					send_message(NACK);
				} else {
					status.paused = false;
				}
			}
			break;

		case WAIT:
			status.paused = true;
			// TODO - maybe? send_message(OKAY);
			break;

		case ERROR:
			send_message(NACK);
			break;

		default:
			break;
	}
}


boolean send_message(int type) {
	return send_message(type, NULL, 0);
}


boolean send_message(int type, uint8_t *payload, int payload_size) {
	memset(packet.bytes, 0, sizeof(packet_t));
	const char* typestr = get_message_type(type);
	memcpy(packet.message.header.magic, MAGIC, 4);
	memcpy(packet.message.header.version, VERSION, 4);
	packet.message.header.sync = millis();
	memcpy(packet.message.header.message_type, typestr, 4);
	memcpy(packet.message.payload, payload, payload_size);
	if (payload_size > 0) {
		packet.message.header.payload_size =
			SMM.encrypt(packet.message.payload, sizeof(packet.message.payload), payload_size, config.param.key);
	}

	print("SEND '");
	print(typestr);
	print("' payload=");
	print(payload_size);
	print(" pad=");
	println(payload_size > 0 ? 4 - (payload_size % 4) : 0);
	print_hex(packet.bytes, sizeof(message_header_t) + payload_size);

	IPAddress remote = config.param.server;
	udp.beginPacket(remote, config.param.port);
	int packet_size = sizeof(message_header_t) + packet.message.header.payload_size;
	udp.write(packet.bytes, packet_size);
	return udp.endPacket() != 0;
}


int receive_message() {
	int packet_size = udp.parsePacket();
	if (packet_size == 0) return NONE;
	IPAddress remote = udp.remoteIP();
	IPAddress pool = config.param.server;
	if (remote != pool) {
		print("Remote: ");
		print(remote);
		print(" Expected: ");
		println(pool);
		udp.flush();
		return NONE;
	}
	if (packet_size < (int) sizeof(message_header_t) || packet_size > MAX_UDP_PAYLOAD) {
		print("ERROR: packet size=");
		println(packet_size);
		udp.flush();
		return ERROR;
	}
	memset(packet.bytes, 0, sizeof(packet_t));
	udp.read(packet.bytes, packet_size);
	int type = get_message_type(packet.message.header.message_type);
	packet.message.header.payload_size =
		SMM.decrypt(packet.message.payload, packet.message.header.payload_size, config.param.key);

	print("RECV '");
	print(get_message_type(type));
	print("' size=");
	println(packet.message.header.payload_size);
	print_hex(packet.bytes, sizeof(message_header_t) + packet.message.header.payload_size);

	return type;
}


const char *get_message_type(int type) {
	switch (type) {
		case PING: return "PING";
		case HELO: return "HELO";
		case SYNC: return "SYNC";

		case NODE: return "NODE";
		case POOL: return "POOL";
		case WAIT: return "WAIT";

		case MINE: return "MINE";
		case LAST: return "LAST";
		case DONE: return "DONE";

		case STAT: return "STAT";
		case INFO: return "INFO";

		case OKAY: return "OKAY";
		default: return "NACK";
	}
}


MessageType get_message_type(uint8_t *cmd) {
	if (strncmp("PING", (char *)cmd, 4) == 0) return PING;
	if (strncmp("HELO", (char *)cmd, 4) == 0) return HELO;
	if (strncmp("SYNC", (char *)cmd, 4) == 0) return SYNC;

	if (strncmp("NODE", (char *)cmd, 4) == 0) return NODE;
	if (strncmp("POOL", (char *)cmd, 4) == 0) return POOL;
	if (strncmp("WAIT", (char *)cmd, 4) == 0) return WAIT;

	if (strncmp("MINE", (char *)cmd, 4) == 0) return MINE;
	if (strncmp("LAST", (char *)cmd, 4) == 0) return LAST;
	if (strncmp("DONE", (char *)cmd, 4) == 0) return DONE;

	if (strncmp("STAT", (char *)cmd, 4) == 0) return STAT;
	if (strncmp("INFO", (char *)cmd, 4) == 0) return INFO;

	return NACK;
}


const char *get_status_type(int type) {
	switch(type) {
		case SMM_NO_SHIELD: return "SMM_NO_SHIELD";
		case SMM_IDLE: return "SMM_IDLE";
		case SMM_READY: return "SMM_READY";
		case SMM_MINING: return "SMM_MINING";
		case SMM_DONE: return "SMM_DONE";
		case SMM_FAIL: return "SMM_FAIL";
		case SMM_INVALID: return "SMM_INVALID";
		default: return "SMM_UNKNOWN";
	}
}

// BLE EVENTS

void blePeripheralConnectHandler(BLECentral& central) {
	print("Connected event, central: ");
	println(central.address());
}

void blePeripheralDisconnectHandler(BLECentral& central) {
	print("Disconnected event, central: ");
	println(central.address());
}

void poolCharacteristicWritten(BLECentral& central, BLECharacteristic& characteristic) {
	print("Write event, central: ");
	println(central.address());
	print("Characteristic written: ");
	println(poolCharacteristic.value());
}

// CONDITIONAL PRINTING

boolean is_tethered() {
	Serial.begin(9600);
	int tries = 3;
	while (tries > 0) {
		if (Serial) {
			return true;
		} else {
			delay(1000);
			tries--;
		}
	}
	return false;
}

void print(int i) {
	if (!status.tethered) return;
	Serial.print(i);
}

void print(const char *message) {
	if (!status.tethered) return;
	Serial.print(message);
}

void println() {
	if (!status.tethered) return;
	Serial.println();
}

void println(int i) {
	if (!status.tethered) return;
	Serial.println(i);
}

void println(const char *message) {
	if (!status.tethered) return;
	Serial.println(message);
}

void print_hex(uint8_t *buf, int length) {
	if (!status.tethered) return;
	for (int i = 0; i < length; i++) {
		if (i > 0 && i % 32 == 0) {
			Serial.println();
		}
		Serial.print(' ');
		int value = buf[i];
		if (value <= 0x0F) {
			Serial.print('0');
		}
		Serial.print(value, HEX);
	}
	Serial.println();
}
