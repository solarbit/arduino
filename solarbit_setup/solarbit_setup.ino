#include <EEPROM.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <SolarBit_SMM.h>

#define MAX_UDP_PAYLOAD 1472
#define UDP_PORT 21314 // "SB"

WiFiUDP udp;

config_t config;
boolean hardware_ok = false;
char input_buffer[64];

typedef enum {
	IDLE = 0,
	SELECT_NETWORK,
	ENTER_PASSWORD,
	ENTER_SERVER,
	ENTER_PORT,
	ENTER_ADDRESS,
	ENTER_KEY
} config_state_t;

config_state_t state = IDLE;
packet_t packet;


void setup() {
	memset(input_buffer, 0, 64);
	Serial.begin(9600);
	while (!Serial);
	Serial.println("SOLARBIT MINING MODULE SETUP");
	Serial.println("SMM Version: Prototype A");
	Serial.println("See: https://www.solarbit.cc");
	Serial.println("----------------------------");
	Serial.println("Performing hardware checks...");

	Serial.print("  SolarBit shield: ");
	if (SMM.status() == SMM_NO_SHIELD) {
		Serial.println("EMULATED");
	} else {
		Serial.println("DETECTED");
	}

	String sfv = SMM.firmwareVersion();
	Serial.print("  SolarBit firmware version: ");
	Serial.println(sfv);
    if (sfv != SMM_FIRMWARE_REQUIRED) {
      Serial.println("  SolarBit Status: NOT PASSED");
      Serial.println("   - The firmware version on the shield do not match the");
      Serial.println("     version required by the library, you may experience");
      Serial.println("     issues or failures.");
	  return;
    }

	Serial.print("  WiFi101 shield: ");
	if (WiFi.status() == WL_NO_SHIELD) {
	  Serial.println("NOT PRESENT");
	  return; // don't continue
	}
	Serial.println("DETECTED");
    String fv = WiFi.firmwareVersion();
	Serial.print("  WiFi firmware version: ");
	Serial.println(fv);
    if (fv != WIFI_FIRMWARE_REQUIRED) {
      Serial.println("  WiFi Status: NOT PASSED");
      Serial.println("   - The firmware version on the shield do not match the");
      Serial.println("     version required by the library, you may experience");
      Serial.println("     issues or failures.");
	  return;
    }
	byte mac[6];
	WiFi.macAddress(mac);
	Serial.print("  MAC Address: ");
	print_mac_address(mac);
	Serial.print("  EEPROM Size: ");
	Serial.println(EEPROM.length());
	hardware_ok = true;
	Serial.println("Hardware OK");
	load_config();
	Serial.println("\nCurrent Configuration: ");
	print_configuration();
//	Serial.println("EEPROM:");
//	dump_eeprom();
	Serial.println("\nCommands: show|configure|write|verify|eeprom|reset");
}


void loop() {
	if (hardware_ok) {
		char *command;
		config_state_t next = state;
		switch (state) {
			case IDLE:
				if (do_user_command()) {
					next = SELECT_NETWORK;
				}
				break;
			case SELECT_NETWORK:
				memcpy(config.param.magic, MAGIC, 4);
				memcpy(config.param.version, VERSION, 4);
				if (select_network()) {
					next = ENTER_PASSWORD;
				} else {
					next = IDLE;
				}
				break;
			case ENTER_PASSWORD:
				char prompt[64];
				snprintf(prompt, 64, "Enter password for '%s' => ", (char *)config.param.ssid);
				command = prompt_input(prompt, true);
				if (command[0]) {
					strncpy((char *)config.param.password, command, 64);
				}
				Serial.print(" Validating connection... ");
				if (connect_to_wifi()) {
					Serial.println("OK");
					next = ENTER_SERVER;
				} else {
					Serial.println("FAILED");
				}
				break;
			case ENTER_SERVER:
				command = prompt_input("Enter your pool server IP => ");
				memset(config.param.server, 0, 4);
				parseIpAddress(config.param.server, command);
				next = ENTER_PORT;
				break;
			case ENTER_PORT:
				command = prompt_input("Enter pool server port => ");
				if (command[0]) {
					config.param.port = atoi(command);
				} else {
					config.param.port = UDP_PORT;
				}
				next = ENTER_ADDRESS;
				Serial.println("Checking Pool Server...");
				if (check_pool() == true) {
					Serial.println("Pool Server ONLINE");
				} else {
					Serial.println("Pool Server OFFLINE");
				}
				udp.stop();
				WiFi.disconnect();
				// config.param.status = WiFi.status();
				break;
			case ENTER_ADDRESS:
				command = prompt_input("Enter bitcoin address => ");
				if (strlen(command) < 40) {
					if (command[0]) {
						strncpy((char *)config.param.address, command, 40);
					}
					next = ENTER_KEY;
				}
				break;
			case ENTER_KEY:
				command = prompt_input("Enter Pool API Key => ");
				if (strlen(command) == 32) {
					parse_hex(command, 32, (uint8_t *)&config.param.key);
					memcpy((char *)config.param.trailer, MAGIC, 4);
					Serial.println("SMM Configured Successfully");
					print_configuration();
					Serial.println("Send 'write' to commit to EEPROM");
					next = IDLE;
				}
				break;
			default:
				command = prompt_input("Enter Command => ");
				Serial.println(command);
				next = IDLE;
				break;
		}
		state = next;
	} else {
		delay(10000);
	}
}


boolean do_user_command() {
	char *command;
	boolean success;

	command = prompt_input("=> ");
	if (strcmp("show", command) == 0) {
		Serial.println("Current Configuration:");
		print_configuration();
	} else if (strcmp("configure", command) == 0) {
		Serial.println("CONFIGURE...");
		WiFi.begin();
		return true;
	} else if (strcmp("eeprom", command) == 0) {
		Serial.println("EEPROM");
		dump_eeprom();
		Serial.println();
	} else if (strcmp("reset", command) == 0) {
		Serial.println("RESET");
		memset(input_buffer, 0, 64);
		memset(config.bytes, 0, sizeof(config_t));
		Serial.println("Clearing EEPROM - PLEASE WAIT");
		for (int i = 0; i < EEPROM.length(); i++) {
			EEPROM.update(i, 0);
			Serial.print('.');
			if (i > 0 && (i % 80) == 0) {
				Serial.println();
			}
		}
		Serial.println();
	} else if (strcmp("write", command) == 0) {
		Serial.println("Writing to EEPROM - PLEASE WAIT");
		success = write_eeprom();
		if (success == true) {
			Serial.println("DONE");
		} else {
			Serial.println("ERROR writing to EEPROM");
		}
		Serial.println();
	} else if (strcmp("verify", command) == 0) {
		Serial.print("WIFI: ");
		if (connect_to_wifi()) {
			Serial.println("CONNECTED");
			Serial.println("Contacting Pool...");
			if (check_pool() == true) {
				Serial.println("POOL: ONLINE");
			} else {
				Serial.println("POOL: OFFLINE");
			}
			udp.stop();
			WiFi.disconnect();
			// config.param.status = WiFi.status();
		} else {
			Serial.println("OFFLINE");
		}
	} else {
		Serial.print("Unknown command: '");
		Serial.print(command);
		Serial.println("'");
	}
	return false;
}


void load_config() {
	for (unsigned int i = 0; i < sizeof(config_t); i++) {
		config.bytes[i] = EEPROM.read(i);
	}
}


boolean is_configuration_valid() {
	return memcmp((const uint8_t*)config.param.magic, MAGIC, 4) == 0
		&& memcmp((const uint8_t*)config.param.trailer, MAGIC, 4) == 0;
}


void print_configuration() {
	if (! is_configuration_valid()) {
		Serial.println("  NOT CONFIGURED");
	} else {
		Serial.print("  SMM Magic: ");
		print_hex(config.param.magic, 4);
		Serial.print("\n  SMM Version: ");
		print_version();
		Serial.print("\n  WiFi Network: ");
		Serial.print((char *)config.param.ssid);
		Serial.print("\n  WiFi Password: ");
		int len = strlen((char *)config.param.password);
		char masked[len + 1];
		for (int i = 0; i < len; i++) {
			masked[i] = '*';
		}
		masked[len] = 0;
		Serial.println(masked);
		Serial.print("  Pool Server: sbmp://");
		print_server_address();
		Serial.print("\n  Address: ");
		Serial.print((char *)config.param.address);
		Serial.print("\n  API Key: ");
		print_hex((uint8_t *)config.param.key, 16);
		Serial.print("\n  Trailer: ");
		print_hex(config.param.trailer, 4);
		Serial.println();
		dump_hex(config.bytes, sizeof(config_t));
		Serial.println('\n');
	}
}


void dump_config() {
	dump_hex(config.bytes, sizeof(config_t));
}


void dump_eeprom() {
	for (int i = 0; i < EEPROM.length(); i++) {
		int value = EEPROM.read(i);
		if (value <= 0x0F) {
			Serial.print('0');
		}
		Serial.print(value, HEX);
		Serial.print(" ");
		if (i > 0 && ((i + 1) % 32 == 0)) {
			Serial.println();
		}
	}
}


boolean write_eeprom() {
	int len = sizeof(config_t);
	Serial.print("Writing ");
	Serial.print(len);
	Serial.println(" bytes");
	for (int i = 0; i < len; i++) {
		uint8_t value = config.bytes[i];
		if (value != EEPROM[i]) {
			EEPROM.write(i, (byte)value);
		}
		if (value == EEPROM[i]) {
			if (value <= 0x0F) {
				Serial.print(" 0");
			} else {
				Serial.print(" ");
			}
			Serial.print(value, HEX);
		} else {
			Serial.println();
			return false;
		}
		if (i > 0 && ((i + 1) % 32) == 0) {
			Serial.println();
		}
	}
	Serial.println();
	return true;
}


boolean select_network() {
	// scan for nearby networks:
	Serial.print("\nScanning Networks...");
	byte num = WiFi.scanNetworks();
	// print the list of networks seen:
	Serial.print("found ");
	Serial.println(num);
	// print the network number and name for each network found:
	for (int i = 0; i < num; i++) {
		Serial.print("  ");
		Serial.print(i + 1);
		Serial.print(") \'");
		Serial.print(WiFi.SSID(i));
		Serial.print("\' Signal:");
		Serial.print(WiFi.RSSI(i));
		Serial.print("dBm");
		Serial.print(" Encryption:");
		Serial.println(WiFi.encryptionType(i));
	}
	char prompt[64];
	snprintf(prompt, 64, "\nSelect a network [1-%u] => ", num);
	char *command = prompt_input(prompt);
	int selected = atoi(command);
	if (selected > 0 && selected <= num) {
		strncpy((char *)config.param.ssid, WiFi.SSID(selected - 1), 32);
		return true;
	} else {
		Serial.print("Invalid Selection: ");
		Serial.println(selected);
		Serial.println("Exiting config");
		return false;
	}
}

char *prompt_input(const char *s) {
	return prompt_input(s, false);
}

char *prompt_input(const char *s, boolean mask) {
	char *command;
	do {
		Serial.print(s);
		int num;
		while (!(num = Serial.available()));
		Serial.readBytes(input_buffer, num);
		input_buffer[num] = 0;
		command = trim_string(input_buffer);
		if (mask) {
			int len = strlen(command);
			char masked[len + 1];
			for (int i = 0; i < len; i++) {
				masked[i] = '*';
			}
			masked[len] = 0;
			Serial.println(masked);
		} else {
			Serial.println(command);
		}
	} while (! command[0]);
	return command;
}


boolean connect_to_wifi() {
	int status = WiFi.status();
	if (status != WL_CONNECTED) {
		int tries = 3;
		while (status != WL_CONNECTED && tries > 0) {
			// Connect to WPA network:
			status = WiFi.begin((char *)config.param.ssid, (char *)config.param.password);
			if (status != WL_CONNECTED) {
				tries--;
				delay(5000);
			}
		}
	}
	return status == WL_CONNECTED;
}


boolean check_pool() {
	udp.begin(config.param.port);
	int tries = 3;
	boolean success = false;

	IPAddress remote = config.param.server;
	Serial.print("Sending HELO to ");
	Serial.print(remote);
	Serial.print(":");
	Serial.print(config.param.port);
	Serial.println();
	while (tries > 0 && !success) {
		if (send_hello()) {
			tries--;
			delay(1000);
			success = receive_sync();
		}
	}
	return success;
}


boolean send_hello() {
	memset(packet.bytes, 0, sizeof(packet_t));
	memcpy(packet.message.header.magic, MAGIC, 4);
	memcpy(packet.message.header.version, VERSION, 4);
	packet.message.header.sync = millis();
	memcpy(packet.message.header.message_type, "HELO", 4);
	packet.message.header.payload_size = 0;
	IPAddress remote = config.param.server;
	udp.beginPacket(remote, config.param.port);
	udp.write(packet.bytes, sizeof(message_header_t));
	return udp.endPacket() != 0;
}


boolean receive_sync() {
	memset(packet.bytes, 0, sizeof(packet_t));
	int packet_size = receive_packet(packet.bytes, sizeof(packet_t));
	if (packet_size <= 0) return NONE;
	if (packet_size < (int) sizeof(message_header_t)) {
		Serial.print("ERROR: packet size=");
		Serial.println(packet_size);
		print_hex(packet.bytes, packet_size);
		return false;
	}
	char typestr[5];
	memcpy(typestr, &packet.message.header.message_type, 4);
	typestr[4] = 0;
	IPAddress remote = config.param.server;
	Serial.print("Received ");
	Serial.print(typestr);
	Serial.print(" from ");
	Serial.print(remote);
	Serial.print(":");
	Serial.print(config.param.port);
	Serial.println();
	return get_message_type(packet.message.header.message_type) == SYNC;
}


int receive_packet(uint8_t *buf, int size) {
	int packet_size = udp.parsePacket();
	if (packet_size == 0) {
		return 0;
	}
	if (packet_size > MAX_UDP_PAYLOAD) {
		udp.flush();
		return 0;
	}
	memset(buf, 0, size);
	udp.read(buf, packet_size);
	return packet_size;
}


int get_message_type(uint8_t *cmd) {
	if (strncmp("PING", (char *)cmd, 4) == 0) return PING;

	if (strncmp("HELO", (char *)cmd, 4) == 0) return HELO;
	if (strncmp("SYNC", (char *)cmd, 4) == 0) return SYNC;
	if (strncmp("NODE", (char *)cmd, 4) == 0) return NODE;
	if (strncmp("POOL", (char *)cmd, 4) == 0) return POOL;

	if (strncmp("MINE", (char *)cmd, 4) == 0) return MINE;
	if (strncmp("DONE", (char *)cmd, 4) == 0) return DONE;
	if (strncmp("WAIT", (char *)cmd, 4) == 0) return WAIT;
	if (strncmp("STAT", (char *)cmd, 4) == 0) return STAT;

	if (strncmp("OKAY", (char *)cmd, 4) == 0) return OKAY;
	if (strncmp("INFO", (char *)cmd, 4) == 0) return INFO;
//	if (strncmp("STOP", (char *)cmd, 4) == 0) return STOP;
//	if (strncmp("TEST", (char *)cmd, 4) == 0) return TEST;
	return NACK;
}


// Parsing from Serial

char *trim_string(char *str) {
	char *end;
	while(isspace(*str)) {
		str++;
	}
	if(*str == 0) { // All spaces?
		return str;
	}
	end = str + strlen(str) - 1;
	while(end > str && isspace(*end)) {
		end--;
	}
	*(end + 1) = 0;
	return str;
}


void parseIpAddress(uint8_t *value, char *str) {
	size_t index = 0;
	while (*str) {
		if (isdigit((uint8_t)*str)) {
			value[index] *= 10;
			value[index] += *str - '0';
		} else {
			index++;
		}
		str++;
	}
}


void parse_hex(char *buf, int size, uint8_t *result) {
	for (int i = 0, j = 0; i < size - 1; i += 2, j++) {
		uint8_t x, y;
		x = hex_to_int(buf[i]);
		y = hex_to_int(buf[i + 1]);
		result[j] = (x << 4) | y;
	}
}


int hex_to_int(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return 0;
}


// Printing to Serial

void print_version() {
	char version[8];
	version[0] = '0' + config.param.version[0];
	version[1] = '.';
	version[2] = '0' + config.param.version[1];
	version[3] = '.';
	version[4] = '0' + config.param.version[2];
	version[5] = '-';
	version[6] = config.param.version[3];
	version[7] = 0;
	Serial.print(version);
}

void print_server_address() {
	uint8_t *address = config.param.server;
	for (int i = 0; i < 3; i++) {
		Serial.print(address[i]);
		Serial.print(".");
	}
	Serial.print(address[3]);
	Serial.print(":");
	Serial.print(config.param.port);
}


void print_mac_address(byte *mac) {
	for (int i = 5; i > 0; i--) {
		if (mac[i] <= 0x0F) {
			Serial.print('0');
		}
		Serial.print(mac[i], HEX);
		Serial.print(":");
	}
	Serial.println(mac[0],HEX);
}


void print_ip_address(IPAddress addr) {
	Serial.print(addr[0]);
	Serial.print(".");
	Serial.print(addr[1]);
	Serial.print(".");
	Serial.print(addr[2]);
	Serial.print(".");
	Serial.print(addr[3]);
}


void print_hex(uint8_t *buf, int length) {
	for (int i = 0; i < length; i++) {
		int value = buf[i];
		if (value <= 0x0F) {
			Serial.print('0');
		}
		Serial.print(value, HEX);
	}
}


void dump_hex(uint8_t *buf, int length) {
	for (int i = 0; i < length; i++) {
		int value = buf[i];
		if (value <= 0x0F) {
			Serial.print(" 0");
		} else {
			Serial.print(" ");
		}
		Serial.print(value, HEX);
		if (i > 0 && ((i + 1) % 32 == 0)) {
			Serial.println();
		}
	}
}
