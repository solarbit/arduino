#include <sha256.h>

#define HASH_SIZE 32
#define BLOCK_HEADER_SIZE 80

typedef struct {
	uint32_t version;
	uint8_t previous_block[HASH_SIZE];
	uint8_t merkle_root[HASH_SIZE];
	uint32_t time;
	uint32_t bits;
	uint32_t nonce;
} block_header_t;


typedef union {
	block_header_t header;
	uint8_t packet[sizeof(block_header_t)];
} block_t;

block_t block;


typedef struct {
	uint8_t target[HASH_SIZE];
	uint8_t current_hash[HASH_SIZE];
	uint8_t best_hash[HASH_SIZE];
	unsigned long start_time;
	unsigned long cumulative_time;
	uint32_t start_nonce;
	boolean valid;
	boolean mined;
	boolean paused;
} status_t;

status_t status;

uint8_t const max_hash[] = {
	0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// M02000000da388d7496041fc6715f6fb06d93c49145afb6c25d872ebd0500000000000000425f3c31f49ce7dece098cc6236fc0c9fb099d4e73609d48d7ff42313468aa081c48a2524212061929e06286
uint8_t test_block[] = {
	1,0,0,0,234,141,253,92,14,35,8,200,179,166,115,97,93,250,242,143,169,3,156,
	88,73,250,88,75,5,15,0,0,0,0,0,0,34,89,134,254,85,127,253,145,191,7,250,55,
	82,184,161,249,5,88,122,137,30,126,220,11,228,65,119,50,21,45,140,229,12,234,
	249,77,133,33,19,26,112,140,77,210
};


void setup() {
	initialize_wireless_network();
	connect_to_server();
	status.paused = true;
}


void loop() {
	do_server_command();
	if (status.valid && !status.paused && !status.mined) {
		do_hash(&block, status.current_hash);
		int compare = memcmp(status.current_hash, status.target, HASH_SIZE);
		if (compare <= 0) {
			status.mined = true;
			status.paused = true;
			status.cumulative_time += millis() - status.start_time;
			for (int i = 0; i < HASH_SIZE; i++) {
				status.best_hash[i] = status.current_hash[i];
			}
			print_status();
		} else {
			int compare2 = memcmp(status.current_hash, status.best_hash, HASH_SIZE);
			if (compare2 <= 0) {
				for (int i = 0; i < HASH_SIZE; i++) {
					status.best_hash[i] = status.current_hash[i];
				}
				print_update();
			}
			block.header.nonce += 1;
		}
	}
}


void load_block(uint8_t *bytes) {
	status.valid = false;
	for (int i = 0; i < BLOCK_HEADER_SIZE; i++) {
		block.packet[i] = bytes[i];
	}
	if (block.header.version > 0) {
		status.valid = true;
	}
	status.valid = set_target(block.header.bits, status.target);
	for (int i = 0; i < HASH_SIZE; i++) {
		status.current_hash[i] = max_hash[i];
		status.best_hash[i] = max_hash[i];
	}
	status.start_time = 0;
	status.cumulative_time = 0;
	status.start_nonce = block.header.nonce;
	status.mined = false;
	status.paused = true;
}


boolean set_target(uint32_t bits, uint8_t *target) {
	for (int i = 0; i < HASH_SIZE; i++) {
		target[i] = 0;
	}
	uint32_t mantissa = bits & 0x00FFFFFF;
	uint8_t exponent = bits >> 24 & 0x1F;
	if (exponent > 3) {
		target[32 - exponent] = mantissa >> 16 & 0xFF;
		target[33 - exponent] = mantissa >> 8 & 0xFF;
		target[34 - exponent] = mantissa & 0xFF;
		return true;
	} else {
		return false;
	}
}


double get_hashrate() {
	double hashes = (block.header.nonce - status.start_nonce) / 1000.0;
	if (status.paused) {
		if (status.cumulative_time > 0) {
			return hashes / (status.cumulative_time / 1000.0);
		} else {
			return 0.0;
		}
	} else {
		return hashes / ((millis() - status.start_time + status.cumulative_time) / 1000.0);
	}
}


void do_hash(block_t *block, uint8_t *hash) {
	uint8_t temp[HASH_SIZE];
	sha256_digest(block->packet, sizeof(block_t), temp);
	uint8_t temp2[HASH_SIZE];
	sha256_digest(temp, HASH_SIZE, temp2);
	for (int i = 0, j = 31; i < HASH_SIZE; i++, j--) {
		hash[i] = temp2[j];
	}
}


// TODO: Wireless Shield connection
void initialize_wireless_network() {
	//
}

// TODO: UDP Protocol
void connect_to_server() {
	Serial.begin(9600);
	while (!Serial);
	Serial.println("Miner Ready");
}


void do_server_command() {
	char ch = 0;
	if (Serial.available()) {
		ch = Serial.read();
	}
  //  block_header header;
    if (ch) {
      switch (ch) {
        case 'm':
        case 'M':
			char buf[161];
			int num;
			num = Serial.readBytes(buf, 160);
			if (num == 160) {
				uint8_t bytes[80];
				from_hex(buf, 160, bytes);
				load_block(bytes);
				if (status.valid) {
					Serial.println("\nMINING BLOCK...");
					print_hex(status.target, HASH_SIZE);
					Serial.println(":target");
					status.paused = false;
				} else {
					Serial.println("INVALID BLOCK");
				}
				print_block();
			} else {
				Serial.print("ERROR: ");
				Serial.println(buf);
			}
			break;
        case 's':
        case 'S':
			print_status();
    		break;
		case 'p':
		case 'P':
			if (status.valid && !status.mined) {
				status.paused = !status.paused;
				if (status.paused) {
					status.cumulative_time += millis() - status.start_time;
					Serial.println("\nPAUSED");
				} else {
					Serial.println("\nRESUMING");
					status.start_time = millis();
				}
			} else {
				Serial.println("WAITING");
			}
			break;
		case 't':
  		case 'T':
			load_block(test_block);
			Serial.println("TEST BLOCK LOADED...");
			print_block();
			print_hex(status.target, HASH_SIZE);
			Serial.println(":target");
			status.paused = false;
			break;
        default:
			Serial.print("UNKNOWN COMMAND '");
        	Serial.print(ch);
        	Serial.println("'");
        	break;
      };
    }
}


boolean notify_server(block_t *block) {
	return false;
}


void print_block() {
	Serial.print("version:");
	Serial.println(block.header.version);
	Serial.print("previous:");
	print_hex(block.header.previous_block, HASH_SIZE);
	Serial.print("\nmerkle root:");
	print_hex(block.header.merkle_root, HASH_SIZE);
	Serial.print("\ntimestamp:");
	Serial.println(block.header.time);
	Serial.print("bits:");
	Serial.println(block.header.bits);
	Serial.print("starting nonce:");
	Serial.println(block.header.nonce);
	Serial.println();
}


void print_update() {
	print_hex(status.best_hash, HASH_SIZE);
	Serial.print(":");
	Serial.println(block.header.nonce);
}


void print_status() {
	Serial.print("\nSTATUS: ");
	if (status.mined) {
		Serial.println("MINED");
	} else if (!status.paused) {
		Serial.println("MINING");
	} else if (status.valid) {
		Serial.println("PAUSED");
	} else {
		Serial.println("WAITING");
	}
	unsigned long time = status.cumulative_time;
	if (!status.paused && !status.mined) {
		time += millis() - status.start_time;
	}
	print_hex(status.target, HASH_SIZE);
	Serial.println(":target");
	print_hex(status.best_hash, HASH_SIZE);
	Serial.println(":best");
	print_hex(status.current_hash, HASH_SIZE);
	Serial.print(":");
	Serial.println(block.header.nonce);
	Serial.print(time);
	Serial.println(":ms");
	Serial.print(get_hashrate());
	Serial.println(":KH/s");
}


void from_hex(char *buf, int size, uint8_t *result) {
	for (int i = 0, j = 0; i < size; i += 2, j++) {
		uint8_t x, y;
		x = hex_to_int(buf[i]);
		y = hex_to_int(buf[i + 1]);
		result[j] = (x << 4) | y;
	}
}

uint8_t hex_to_int(char c) {
	char const *map = "0123456789abcdef";
	for (int i = 0; i < 16; i++) {
		if (c == map[i]) {
			return i;
		}
	}
	return 0;
}

void print_hex(uint8_t* bytes, unsigned int count) {
	char result[count * 2 + 1];
	for (unsigned int i = 0, j = 0; i < count; i++, j += 2) {
		result[j] = "0123456789abcdef"[bytes[i] >> 4];
		result[j + 1] = "0123456789abcdef"[bytes[i] & 15];
	}
	result[count * 2] = 0;
	Serial.print(result);
}
