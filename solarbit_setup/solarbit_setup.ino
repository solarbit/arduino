#include <EEPROM.h>
#include <WiFi101.h>
#include <WiFiUdp.h>

uint8_t MAGIC[] = "SMM";
uint8_t VERSION[] = {1, 0, 0, 'A'};

#define MAX_UDP_PAYLOAD 1472
#define UDP_PORT 21314 // "SB"

WiFiUDP udp;

enum Protocol {
	// Common
	PING = 0,
	HELO,
	NACK, // maybe
	// POOL
	POOL,
	STAT,
	WAIT,
	MINE,
	TEST, // TEMP
	POST, // reset - maybe?
	// SMM
	INFO,
	DONE, // TODO: Encryption
	BEST, // TODO: Encryption
};

typedef struct {
	uint8_t magic[4];
	uint8_t version[4];
	uint32_t nonce;
	uint8_t message_type[4];
	uint32_t payload_size;
} message_header_t;

typedef union {
	message_header_t header;
	uint8_t bytes[sizeof(message_header_t)];
} message_t;


typedef struct {
	uint8_t magic[4];
	uint8_t version[4];
	uint8_t ssid[32];
	uint8_t password[64];
	uint8_t server[4];
	uint16_t port;
	uint8_t address[40];
	uint8_t status;
} net_config_t;

typedef union {
	net_config_t net;
	uint8_t bytes[sizeof(net_config_t)];
} config_t;

config_t config;
boolean hardware_ok = false;
char input_buffer[64];

typedef enum {
	IDLE = 0,
	SELECT_NETWORK,
	ENTER_PASSWORD,
	ENTER_SERVER,
	ENTER_PORT,
	ENTER_ADDRESS
} config_field_t;

config_field_t field = IDLE;

uint8_t response[MAX_UDP_PAYLOAD];

void setup() {
	memset(input_buffer, 0, 64);
	Serial.begin(9600);
	while (!Serial);
	Serial.println("SOLARBIT MINING MODULE SETUP");
	Serial.println("SMM Version: Prototype A");
	Serial.println("See: https://www.solarbit.cc");
	Serial.println("----------------------------");
	Serial.println("Performing hardware checks...");
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
		config_field_t next = field;
		boolean success;
		switch (field) {
			case IDLE:
				command = prompt_input("=> ");
				if (strcmp("show", command) == 0) {
					print_configuration();
				} else if (strcmp("configure", command) == 0) {
					Serial.println("CONFIGURE...");
					WiFi.begin();
					next = SELECT_NETWORK;
				} else if (strcmp("eeprom", command) == 0) {
					Serial.println("EEPROM");
					dump_eeprom();
					Serial.println();
				} else if (strcmp("reset", command) == 0) {
					Serial.println("RESET");
					memset(input_buffer, 0, 64);
					memset(config.bytes, 0, sizeof(net_config_t));
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
						Serial.println("\nOK");
					} else {
						Serial.println("\nERROR");
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
					} else {
						Serial.println("OFFLINE");
					}
				} else {
					Serial.print("Unknown command: '");
					Serial.print(command);
					Serial.println("'");
				}
				break;
			case SELECT_NETWORK:
				memcpy(config.net.magic, MAGIC, 4);
				memcpy(config.net.version, VERSION, 4);
				config.net.status = WL_IDLE_STATUS;
				next = select_network();
				// next = ENTER_PASSWORD;
				break;
			case ENTER_PASSWORD:
				char prompt[64];
				snprintf(prompt, 64, "Enter password for '%s' => ", (char *)config.net.ssid);
				command = prompt_input(prompt, true);
				if (command[0]) {
					strncpy((char *)config.net.password, command, 64);
				}
				Serial.print(" Validating connection... ");
				success = connect_to_wifi();
				if (success == true) {
					Serial.println("OK");
					next = ENTER_SERVER;
				} else {
					Serial.println("FAILED");
				}
				break;
			case ENTER_SERVER:
				command = prompt_input("Enter your pool server IP => ");
				memset(config.net.server, 0, 4);
				parseIpAddress(config.net.server, command);
				next = ENTER_PORT;
				break;
			case ENTER_PORT:
				command = prompt_input("Enter pool server port => ");
				if (command[0]) {
					config.net.port = atoi(command);
				} else {
					config.net.port = UDP_PORT;
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
				break;
			case ENTER_ADDRESS:
				command = prompt_input("Enter bitcoin address => ");
				if (strlen(command) < 40) {
					if (command[0]) {
						strncpy((char *)config.net.address, command, 40);
					}
					print_configuration();
					next = IDLE;
				}
				break;
			default:
				command = prompt_input("Enter Command => ");
				Serial.println(command);
				next = IDLE;
				break;
		}
		field = next;
	} else {
		delay(10000);
	}
}


void load_config() {
	for (unsigned int i = 0; i < sizeof(config_t); i++) {
		config.bytes[i] = EEPROM.read(i);
	}
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

void print_configuration() {
	if (memcmp((const uint8_t*)config.net.magic, MAGIC, 4)) {
		Serial.println("  NOT CONFIGURED");
	} else {
		Serial.println("CONFIGURATION");
		Serial.print("  SMM Magic: ");
		print_hex(config.net.magic, 4);
		Serial.print("\n  SMM Version: ");
		print_version();
		Serial.print("\n  WiFi Network: ");
		Serial.println((char *)config.net.ssid);
		Serial.print("  WiFi Password: ");
		int len = strlen((char *)config.net.password);
		char masked[len + 1];
		for (int i = 0; i < len; i++) {
			masked[i] = '*';
		}
		masked[len] = 0;
		Serial.println(masked);
		Serial.print("  Pool Server: smm://");
		print_server_address();
		Serial.print("\n  Address: ");
		Serial.println((char *)config.net.address);
		Serial.print("  Status: ");
		Serial.println(config.net.status);
		Serial.println();
		dump_hex(config.bytes, sizeof(config_t));
		Serial.println();
	}
}

void print_version() {
	char version[8];
	version[0] = '0' + config.net.version[0];
	version[1] = '.';
	version[2] = '0' + config.net.version[1];
	version[3] = '.';
	version[4] = '0' + config.net.version[2];
	version[5] = '-';
	version[6] = config.net.version[3];
	version[7] = 0;
	Serial.print(version);
}

void print_server_address() {
	uint8_t *address = config.net.server;
	for (int i = 0; i < 3; i++) {
		Serial.print(address[i]);
		Serial.print(".");
	}
	Serial.print(address[3]);
	Serial.print(":");
	Serial.print(config.net.port);
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
	// dump_hex(config.bytes, sizeof(config_t));
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
		if (i > 0 && (i % 32) == 0) {
			Serial.println();
		}
	}
	Serial.println();
	return true;
}

config_field_t select_network() {
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
		strncpy((char *)config.net.ssid, WiFi.SSID(selected - 1), 32);
		return ENTER_PASSWORD;
	} else {
		Serial.print("Invalid Selection: ");
		Serial.println(selected);
		Serial.println("Exiting config");
		return IDLE;
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

boolean connect_to_wifi() {
	config.net.status = WL_IDLE_STATUS;
	if (config.net.status != WL_CONNECTED) {
		int tries = 3;
		while (config.net.status != WL_CONNECTED && tries > 0) {
			// Connect to WPA/WPA2 network:
			config.net.status = WiFi.begin((char *)config.net.ssid, (char *)config.net.password);
			if (config.net.status != WL_CONNECTED) {
				tries--;
				delay(5000);
			}
		}
	}
	return config.net.status == WL_CONNECTED;
}


boolean check_pool() {
	udp.begin(config.net.port);
	int tries = 3;
	int success = 0;

	IPAddress remote = config.net.server;
	Serial.print("Sending HELO to ");
	Serial.print(remote);
	Serial.print(":");
	Serial.print(config.net.port);
	Serial.println();

	message_t m = build_message_header("HELO", 0);
	dump_hex(m.bytes, sizeof(message_t));
	Serial.println();
	while (tries > 0 && !success) {
		if (send_packet(m.bytes, sizeof(message_t))) {
			tries--;
			delay(1000);
			int packet_size = receive_packet(response, MAX_UDP_PAYLOAD);
			if (packet_size == sizeof(message_t)) {
				message_t m2;
				int type = parse_message_header(response, sizeof(message_t), &m2);
				Serial.print("Received ");
				Serial.print(get_message_type(type));
				Serial.print(" from ");
				Serial.print(remote);
				Serial.print(":");
				Serial.print(config.net.port);
				Serial.println();
				dump_hex(m2.bytes, sizeof(message_t));
				Serial.println();
				success = (type == HELO);
			}
		} else {
			Serial.println("Packet Send Error");
		}
	}
	return success > 0;
}

message_t build_message_header(const char *type, int payload_size) {
	message_t m;
	memcpy(m.header.magic, MAGIC, 4);
	memcpy(m.header.version, VERSION, 4);
	m.header.nonce = millis();
	memcpy(m.header.message_type, type, 4);
	m.header.payload_size = payload_size;
	return m;
}


int parse_message_header(uint8_t *buf, size_t size, message_t *m) {
	if (size >= sizeof(message_t)) {
		memcpy(m->bytes, buf, sizeof(message_t));
		return get_message_type(m->header.message_type);
	} else {
		return -1;
	}
}


const char *get_message_type(int type) {
	switch (type) {
		case PING: return "PING";
		case HELO: return "HELO";
		case INFO: return "INFO";
		case DONE: return "DONE";
		case BEST: return "BEST";
		default: return "NACK";
	}
}


int get_message_type(uint8_t *cmd) {
	if (strncmp("HELO", (char *)cmd, 4) == 0) return HELO;
	if (strncmp("POOL", (char *)cmd, 4) == 0) return POOL;
	if (strncmp("MINE", (char *)cmd, 4) == 0) return MINE;
	if (strncmp("STAT", (char *)cmd, 4) == 0) return STAT;
	if (strncmp("WAIT", (char *)cmd, 4) == 0) return WAIT;
	if (strncmp("TEST", (char *)cmd, 4) == 0) return TEST;
	return NACK;
}


boolean send_packet(uint8_t *bytes, size_t size) {
	IPAddress remote = config.net.server;
	udp.beginPacket(remote, config.net.port);
	udp.write(bytes, size);
	return udp.endPacket() != 0;
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
