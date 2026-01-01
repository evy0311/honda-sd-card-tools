#include <Arduino.h>
#include <SD.h>        // danman fork, provides Sd2Card + lockUnlockCard etc

#ifndef CHIP_SELECT
#define CHIP_SELECT 10
#endif

// CMD42 flag bits (same as danman example)
#define ERASE       0x08
#define LOCK_UNLOCK 0x04
#define CLR_PWD     0x02
#define SET_PWD     0x01

#define HASH_LBA    0      // <--- put your first CMD18 LBA here
#define HASH_COUNT  256    // number of 512B sectors to hash (e.g., 256 = 128 KiB)

Sd2Card card;

// password sequence
// FE | 00    | 10     | B2 BA B3 BC B0 DF B7 B0 B1 BB BE CE CB B2 DF DF | E1 D9 | ...

// Your 16-byte password (from earlier reverse work)
static uint8_t password[16] = {
    0xB2, 0xBA, 0xB3, 0xBC,
    0xB0, 0xDF, 0xB7, 0xB0,
    0xB1, 0xBB, 0xBE, 0xCE,
    0xCB, 0xB2, 0xDF, 0xDF
};

// ---------- Helpers ----------
// ---- Minimal param reader (little-endian 32-bit) ----
uint32_t crc32_update(uint32_t c, uint8_t b){
  c ^= b;
  for (int i=0;i<8;i++) c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
  return c;
}
uint32_t hashRangeLBA(uint32_t lba, uint32_t count){
  static uint8_t buf[512];
  uint32_t c = 0xFFFFFFFF;
  for (uint32_t i=0;i<count;i++){
    if(!card.readBlock(lba+i, buf)) return 0;   // 0 indicates read error
    for (int j=0;j<512;j++) c = crc32_update(c, buf[j]);
    if ((i & 0x3FF)==0) Serial.print('.');      // progress dot each 1024 sectors
    yield();
  }
  return ~c;
}


// read 4 LE bytes from serial (blocking)
uint32_t readLE32() {
    Serial.println("reading LE32...");
  uint8_t b[4];
  for (int i=0;i<4;i++){ while(!Serial.available()){} b[i]=Serial.read(); }
  return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
}

// prints only the 32-bit little-endian block count, no text
void print_block_count_le() {
  uint32_t blocks = card.cardSize();
  uint8_t bc[4] = { (uint8_t)(blocks & 0xFF), (uint8_t)((blocks>>8)&0xFF),
                    (uint8_t)((blocks>>16)&0xFF), (uint8_t)((blocks>>24)&0xFF) };
  Serial.write(bc, 4);
  Serial.flush();
}

void printCid()
{
    cid_t cid;
    if (!card.readCID(&cid)) {
        Serial.println("readCID() failed");
        return;
    }

    // Treat cid struct as raw 16 bytes
    uint8_t *raw = (uint8_t *)&cid;

    Serial.print("CID: ");
    for (int i = 0; i < 16; i++) {
        if (raw[i] < 0x10) Serial.print('0');
        Serial.print(raw[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
}


bool readBlock0Preview()
{
    uint8_t buf[512];

    // danman Sd2Card: use readBlock(block, dst) for full 512 bytes
    if (!card.readBlock(0, buf)) {
        Serial.println("readBlock(0) failed (locked or error).");
        return false;
    }

    Serial.println("First 64 bytes of block 0:");
    for (int i = 0; i < 64; i++) {
        if (i % 16 == 0) Serial.println();
        if (buf[i] < 16) Serial.print('0');
        Serial.print(buf[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
    return true;
}

bool sdInitBasic()
{
    Serial.print("Initializing card on CS=");
    Serial.println(CHIP_SELECT);

    if (!card.init(SPI_HALF_SPEED, CHIP_SELECT)) {
        Serial.print("card.init() failed, error=0x");
        Serial.println(card.errorCode(), HEX);
        return false;
    }

    Serial.println("Card init OK.");
    Serial.print("Card type: ");
    switch (card.type()) {
        case SD_CARD_TYPE_SD1:  Serial.println("SD1"); break;
        case SD_CARD_TYPE_SD2:  Serial.println("SD2"); break;
        case SD_CARD_TYPE_SDHC: Serial.println("SDHC/SDXC"); break;
        default:                Serial.println("Unknown"); break;
    }

    uint32_t blocks = card.cardSize();
    Serial.print("Card size (blocks): ");
    Serial.println(blocks);
    Serial.print("Card size (MB): ");
    Serial.println((blocks / 2048.0f), 2);

    return true;
}

int lockUnlock_with_flags(uint8_t flags, uint8_t n, const uint8_t *pwd)
{
    Serial.print("lockUnlockCard flags=0x");
    Serial.print(flags, HEX);
    Serial.print(" len=");
    Serial.println(n);

    int ret = card.lockUnlockCard(flags, n, (uint8_t*)pwd);
    Serial.print("lockUnlockCard() ret=");
    Serial.println(ret);
    return ret;
}

// ---- Convenience wrappers ----

void cmd_unlock()
{
    Serial.println("[CMD42 UNLOCK] flags=0x00, len=16");
    lockUnlock_with_flags(0x00, 16, password);
}

void cmd_set_and_lock()
{
    // Sets the password (SET_PWD) and locks the card (LOCK_UNLOCK) in one call.
    // Flags: SET_PWD | LOCK_UNLOCK
    Serial.println("[CMD42 SET+LOCK] flags=SET_PWD | LOCK_UNLOCK, len=16");
    uint8_t flags = SET_PWD | LOCK_UNLOCK;
    lockUnlock_with_flags(flags, 16, password);
}

void cmd_clear_pwd()
{
    Serial.println("[CMD42 CLEAR PASSWORD] flags=CLR_PWD, len=16");
    lockUnlock_with_flags(CLR_PWD, 16, password);
}

// ---------- Dump routine ----------
// This will: ensure card initialized/unlocked, then stream the raw image over Serial.
// Protocol:
//  - send ASCII header "STARTIMG\n"
//  - send 4 bytes LE: block_count (uint32_t little-endian)
//  - send block_count * 512 raw bytes
// Important: caller must capture raw bytes on host side (we provide a Python script).

bool dump_card_over_serial()
{
    Serial.println(F("[DUMP] starting sequence"));

    // first unlock card (attempt)
    Serial.println(F("[DUMP] Attempting CMD42 UNLOCK..."));
    int r = lockUnlock_with_flags(0x00, 16, password);
    if (r != 0 && r != 1) {
        Serial.print("lockUnlock returned unexpected code: ");
        Serial.println(r);
        // continue anyway: sometimes vendor behavior returns 1 on success or 0 depending on lib
    }

    delay(50);

    // init card after unlocking (some cards need re-init)
    if (!card.init(SPI_FULL_SPEED, CHIP_SELECT)) {
        Serial.print("card.init() after unlock failed, error=0x");
        Serial.println(card.errorCode(), HEX);
        return false;
    }

    uint32_t blocks = card.cardSize();
    if (blocks == 0 || blocks > 0x40000000UL) {
        Serial.println("invalid block count");
        return false;
    }
    Serial.print("[DUMP] blocks=");
    Serial.println(blocks);

    // send ASCII header
    Serial.print("STARTIMG\n");

    // send 4-byte little endian block count
    uint8_t bc[4];
    bc[0] = blocks & 0xFF;
    bc[1] = (blocks >> 8) & 0xFF;
    bc[2] = (blocks >> 16) & 0xFF;
    bc[3] = (blocks >> 24) & 0xFF;
    Serial.write(bc, 4);
    Serial.flush(); // try to push header before big stream

    // read + stream each block
    uint8_t buf[512];
    for (uint32_t b = 0; b < blocks; ++b) {
        bool ok = false;
        // try a few times if transient read fails
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (card.readBlock(b, buf)) { ok = true; break; }
            delay(5);
        }
        if (!ok) {
            // If readBlock fails, send a block of zeros in place and report error
            Serial.print("[DUMP] readBlock failed at block ");
            Serial.println(b);
            memset(buf, 0, sizeof(buf));
        }

        // stream raw 512 bytes
        size_t written = Serial.write(buf, 512);
        (void)written;
        // optional: print progress every N blocks
        if ((b & 0x3FF) == 0) { // every 1024 blocks
            Serial.print("[DUMP] streamed block ");
            Serial.println(b);
        }
        // small yield so USB can breathe
        if (b % 8 == 0) delay(0);
    }

    Serial.println();
    Serial.println("[DUMP] completed streaming image. Do NOT remove card until host finishes writing.");
    Serial.flush();
    return true;
}

// streams EXACTLY blocks*512 bytes; do NOT print anything inside
void dump_card_raw_only() {
  // (optional) unlock; ignore return code
  card.lockUnlockCard(0x00, 16, (uint8_t*)password);
  card.init(SPI_FULL_SPEED, CHIP_SELECT);

  uint32_t blocks = card.cardSize();
  static uint8_t buf[512];
  for (uint32_t b = 0; b < blocks; ++b) {
    if (!card.readBlock(b, buf)) memset(buf, 0, 512);  // keep length consistent
    Serial.write(buf, 512);
  }
  Serial.flush();
}

void showMenu()
{
    Serial.println();
    Serial.println("Commands:");
    Serial.println("  i = init card");
    Serial.println("  c = show CID");
    Serial.println("  b = read first 64 bytes of block 0");
    Serial.println("  u = CMD42 unlock with current password");
    Serial.println("  l = CMD42 set password (from password[16]) + lock (SET_PWD + LOCK_UNLOCK)");
    Serial.println("  x = CMD42 clear password (CLR_PWD)");
    Serial.println("  d = DUMP whole card (raw image) over serial");
    Serial.println("  H = CRC32 over range (send 8 bytes: <LBA_LE><COUNT_LE>; prints CRC32)");
    Serial.println("  K = CRC32 over hard-coded range (HASH_LBA/HASH_COUNT)");
    Serial.println("  h/? = help");
    Serial.println();
}

// ---------- Setup / Loop ----------

void setup()
{
    Serial.begin(115200);
    while (!Serial) { }

    Serial.println("[Teensy41 + danman SD CMD42 tool]");
    Serial.println("Wiring (SPI mode):");
    Serial.println("  CS  (DAT3) -> pin 10");
    Serial.println("  MOSI(CMD)  -> pin 11");
    Serial.println("  MISO(D0)   -> pin 12");
    Serial.println("  SCK(CLK)   -> pin 13");
    Serial.println("  3.3V / GND");
    showMenu();
}

void loop()
{
    if (Serial.available()) {
        char ch = Serial.read();

        switch (ch) {
            case 'i':
                sdInitBasic();
                break;

            case 'c':
                printCid();
                break;

            case 'b':
                readBlock0Preview();
                break;

            case 'u':
                cmd_unlock();
                break;

            case 'x':
                cmd_clear_pwd();
                break;
                
            case 'l':
                cmd_set_and_lock();
                break;

            case 'd':
                dump_card_over_serial();
                break;

            case 'H': {
                // Expect 8 bytes after 'H': LBA (LE) then COUNT (LE)
                uint32_t lba = readLE32();
                uint32_t cnt = readLE32();
                // make sure card is inited fast
                if (!card.init(SPI_FULL_SPEED, CHIP_SELECT)) card.init(SPI_HALF_SPEED, CHIP_SELECT);
                uint32_t crc = hashRangeLBA(lba, cnt);
                Serial.print("CRC32 0x"); Serial.println(crc, HEX);
                break;
            }

            case 'K': { // K = hard-coded CRC32 over range
                Serial.println("[K] init+unlock+CRC32 over hard-coded range");
                if (!card.init(SPI_FULL_SPEED, CHIP_SELECT)) {
                Serial.print("init fail err=0x"); Serial.println(card.errorCode(), HEX);
                break;
                }
                // try unlock but don’t abort if return code is odd
                card.lockUnlockCard(0x00, 16, (uint8_t*)password);
                // (re)init after unlock (some cards like this)
                card.init(SPI_FULL_SPEED, CHIP_SELECT);

                Serial.print("Hashing LBA "); Serial.print((uint32_t)HASH_LBA);
                Serial.print(" count "); Serial.println((uint32_t)HASH_COUNT);
                uint32_t crc = hashRangeLBA(HASH_LBA, HASH_COUNT);
                Serial.println();
                Serial.print("CRC32 0x"); Serial.println(crc, HEX);
                break;
            }

            case 'n': { // binary: returns 4 LE bytes = block count
                if (!card.init(SPI_FULL_SPEED, CHIP_SELECT)) card.init(SPI_HALF_SPEED, CHIP_SELECT);
                print_block_count_le();
                break;
            }
            case 'R': { // RAW dump: no header/logs; EXACT bytes only
                if (!card.init(SPI_FULL_SPEED, CHIP_SELECT)) card.init(SPI_HALF_SPEED, CHIP_SELECT);
                dump_card_raw_only();
                break;
            }

            case 'h':
            case '?':
                showMenu();
                break;

            default:
                Serial.print("Unknown cmd: ");
                Serial.println(ch);
                showMenu();
                break;
        }
    }
}