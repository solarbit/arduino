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

uint8_t target[HASH_SIZE];
uint8_t current_hash[HASH_SIZE];
uint8_t best_hash[HASH_SIZE] = {
	0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

boolean valid = false;
boolean mined = false;
boolean paused = true;

unsigned long start_time;
unsigned long cumulative_time;
uint32_t start_nonce;


void setup() {
	initialize_wireless_network();
	connect_to_server();
}


void loop() {
	do_server_command();
	if (valid && !paused && !mined) {
		do_hash(&block, current_hash);
		int compare = memcmp(current_hash, target, HASH_SIZE);
		if (compare <= 0) {
			mined = true;
			paused = true;
			cumulative_time += millis() - start_time;
			for (int i = 0; i < HASH_SIZE; i++) {
				best_hash[i] = current_hash[i];
			}
			print_result();
		} else {
			int compare2 = memcmp(current_hash, best_hash, HASH_SIZE);
			if (compare2 <= 0) {
				for (int i = 0; i < HASH_SIZE; i++) {
					best_hash[i] = current_hash[i];
				}
				print_hex(best_hash, HASH_SIZE);
				Serial.print(":");
				Serial.println(block.header.nonce);
			}
			block.header.nonce += 1;
		}
	}
}


void set_target(block_t *block) {
	uint32_t bits = block->header.bits;
	for (int i = 0; i < HASH_SIZE; i++) {
		target[i] = 0;
	}
	uint32_t mantissa = bits & 0x00FFFFFF;
	uint8_t exponent = bits >> 24 & 0x1F;
	if (exponent > 3) {
		target[32 - exponent] = mantissa >> 16 & 0xFF;
		target[33 - exponent] = mantissa >> 8 & 0xFF;
		target[34 - exponent] = mantissa & 0xFF;
	} else {
		// error!
	}
}


double get_hashrate() {
	double hashes = (block.header.nonce - start_nonce) / 1000.0;
	if (paused) {
		if (cumulative_time > 0) {
			return hashes / (cumulative_time / 1000.0);
		} else {
			return 0.0;
		}
	} else {
		return hashes / ((millis() - start_time + cumulative_time) / 1000.0);
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
			if (valid) {
				Serial.println("\nMINING BLOCK...");
				print_hex(block.packet, BLOCK_HEADER_SIZE);
				Serial.println();
				Serial.print("Starting Nonce: ");
				Serial.println(block.header.nonce);
				print_hex(target, HASH_SIZE);
				Serial.println(":target");
				start_time = millis();
				cumulative_time = 0;
				start_nonce = block.header.nonce;
				paused = false;
			} else {
				Serial.println("No block loaded\nSend 'T' to load a test block");
			}
			break;
        case 's':
        case 'S':
        	Serial.print("\nSTATUS: ");
			if (mined) {
				Serial.println("MINED");
				print_status(cumulative_time);
			} else {
				if (!paused) {
					Serial.println("MINING");
					print_status(millis() - start_time + cumulative_time);
				} else if (block.header.version > 0){
					Serial.println("PAUSED");
					print_status(cumulative_time);
				} else {
					Serial.println("WAITING");
				}
			}
    		break;
		case 'p':
		case 'P':
			if (valid) {
				paused = !paused;
				if (paused) {
					cumulative_time += millis() - start_time;
					Serial.println("\nPAUSE");
				} else if (mined) {
					Serial.println("\nALREADY COMPLETED");
				} else {
					Serial.println("\nRESUME");
					start_time = millis();
				}
			} else {
				Serial.println("No block loaded\nSend 'T' to load a test block");
			}
			break;
		case 't':
  		case 'T':
			load_test_block();
			if (block.header.version > 0) {
				valid = true;
			}
			paused = true;
			mined = false;
			Serial.println("\nTest Block Loaded, send 'M' to start mining!");
			break;
        default:
			Serial.print("COMMAND '");
        	Serial.print(ch);
        	Serial.println("' UNKNOWN");
        	break;
      };
    }
}


boolean notify_server(uint8_t *hash) {
	return false;
}


void print_status(unsigned long time) {
	print_hex(target, HASH_SIZE);
	Serial.println(":target");
	print_hex(best_hash, HASH_SIZE);
	Serial.println(":best");
	print_hex(current_hash, HASH_SIZE);
	Serial.print(":");
	Serial.println(block.header.nonce);
	Serial.print(millis() - start_time + cumulative_time);
	Serial.println(":ms");
	Serial.print(get_hashrate());
	Serial.println(":KH/s");
}


void print_result() {
	Serial.println("\nBLOCK MINED!");
	print_hex(target, HASH_SIZE);
	Serial.println(":target");
	print_hex(current_hash, HASH_SIZE);
	Serial.println(":current");
	Serial.print(block.header.nonce);
	Serial.println(":nonce");
	Serial.print(cumulative_time);
	Serial.println(":ms");
	Serial.print(get_hashrate());
	Serial.println(":KH/s");
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

void load_test_block() {
	uint8_t bytes[] = {
		1,0,0,0,234,141,253,92,14,35,8,200,179,166,115,97,93,250,242,143,169,3,156,
		88,73,250,88,75,5,15,0,0,0,0,0,0,34,89,134,254,85,127,253,145,191,7,250,55,
		82,184,161,249,5,88,122,137,30,126,220,11,228,65,119,50,21,45,140,229,12,234,
		249,77,133,33,19,26,64,113,62,210
	};
	for (int i = 0; i < BLOCK_HEADER_SIZE; i++) {
		block.packet[i] = bytes[i];
	}
	set_target(&block);
}
