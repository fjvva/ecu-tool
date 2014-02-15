 /*
EDC15 Toolbox, made by Bi0H4z4rD V0.4
It can read/write flash on these ECUS and read their info.
Supports EDC15P/P+, EDC15V,EDC15VM+ (1MB flash EDC15VM is not supported, do NOT use the tool on this one!! Only 512KB EDC15VM is supported!!);
Improved boot time, now the first stage and bitbang are avoided.
Removed EEPROM operations to have space in flash (Will add on next release)


TODO:
-Need to add cloning function.
-Need to add retry for packets sent while writing to flash without ACK
-Remake the "read info" with the new automatic sw version detection--DONE
-Adapt the automatic sw version detection to all ecus (not only VAG)
-In the definitive hw version, the 33290 CS must be attached to a MCU pin and disabled for programming (LOW)--Pin D5
-Need to add specific errors
-Implement the file indexing--DONE
-Fix the iso_sendstring so it doesnt call the iso_send_byte and turn off/on rx for every byte
-Add support for EDC15VM 1MB flash (Do not use the tool on these yet!!!!)

Know bugs:
-After adding to DB, ECU will be detected as if recovery mode unless you power cycle it.
-After reading info from dead ECU, tool wont be able to detect files (need to check file.close()

*/

//*Trick to save RAM*////
#define flp(string) flashprint(PSTR(string));
#define RXENABLE 2

//****************LCD stuff****************
#include <Wire_400khz.h> 
#include <LiquidCrystal_I2C.h>
//#include <MemoryFree.h>
LiquidCrystal_I2C lcd(0x27,20,4);

 

//This is the push button stuff
const byte OKbuttonPin = 6;
const byte BackbuttonPin = 7;
const byte LeftbuttonPin = 8; 
const byte RightbuttonPin = 9; 


//This is for the menus:
byte optionset=0;


/*SD stuff
 * SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 10
 */
byte SDbuffer[258];//Buffer to be saved on SD
int SDcounter=0;//counter to know how many bytes will be written to the SD
#include <SdFat.h>
#include <SdFatUtil.h>
SdFat SD;
Sd2Card card; 
SdVolume volume; 
const uint8_t SD_CHIP_SELECT = 10;
SdFile myFile;
// store error strings in flash to save RAM 
#define error(s) error_P(PSTR(s))



boolean fail=0;//Used to determine wether a proccess has failed, and therefor, return to main menu
#define K_IN    0 //RX
#define K_OUT   1 //TX
#define ISORequestByteDelay 7
byte b = 0;
word m=0;
word w=0;
int l=0;
byte Jobdone=0;
byte setspeed = 0;
boolean success = 0;
byte iscrc=0;
byte operation;
byte EcuType;//determines which ecu is connected
byte Specific=0;//used for specific ecu command sets
byte recovery=0;


//*****************************These bytes represent the replies that the ECU gives during common parts********************************


//EDC15 stuff
const char EDC15_ECUBytes[] PROGMEM = { 
//0x83, 0x00, 
//0x01, 0x7F, 0x00, 0x13,//These bytes are for the speed negotiation
0x83, 0xF1, 0x01, 0x7F, 0x10, 0x78, 
0x83, 0xF1, 0x01, 0x50, 0x85, 0x2F,
0x81, 0xF1, 0x01, 0xC2,
0x83, 0xF1, 0x01, 0xC1, 0x6B, 0x8F,
0x03, 0x50, 0x85, 0x87,
0x03,0x67,0x42,0x34,//This is the reply for a correct key sent
0x02,0x74,0xFF
};

const char EDC15_Recovery_ECUBytes[] PROGMEM = { 
//0x83, 0xF1, 
//0x01, 0xC1, 0x6B, 0x8F,
0x83,0xF1,0x01, 0x50, 0x85, 0x87,
0x87,0xF1,0x01,0xC3,0x00,0x00,0x50,0x03,0xFF,0x00,
0x82,0xF1,0x01,0xC3,0x03,
0x03,0x67,0x42,0x34,//This is the reply for a correct key sent
0x02,0x74,0xFF
};

const char EDC15P_ECUBytes[] PROGMEM = { 
0x03,0x7F,0x31,0x22, //This is the string that confirms that ECU got the loader running
0x11,0x36,0x0A,0x01,0x04,0x00,0x05,0x00,0x0B,0x00,0x0C,0x00,0x0D,0x00,0x0F,0x00,0x10,0x00,
0x11,0x36,0x0A,0x01,0x04,0x00,0x05,0x00,0x0B,0x00,0x0C,0x00,0x0D,0x00,0x0F,0x00,0x10,0x00,
0x01,0x7F
};

const char EDC15V_ECUBytes[] PROGMEM = { 
0x03,0x7F,0x31,0x22, //This is the string that confirms that ECU got the loader running
};

const char EDC15VM_ECUBytes[] PROGMEM = { 
0x03,0x7F,0x31,0x22, //This is the string that confirms that ECU got the loader running
0x11,0x36,0xDB,0x00,0xF2,0xF0,0x40,0x00,0x26,0xF0,0x08,0x00,0x84,0x00,0x72,0xFB,0xF6,0xF0,
0x11,0x36,0xDB,0x00,0xF2,0xF0,0x40,0x00,0x26,0xF0,0x08,0x00,0x84,0x00,0x72,0xFB,0xF6,0xF0,
0x01,0x7F
};

//********************************These bytes represent the replies that Arduino gives during common processes


//EDC15 suff
const char EDC15ArduBytes[] PROGMEM = {
0x82,0x01,0xF1,0x10,0x85,
0x81,0x01,0xF1,0x82,
0x03,0x10,0x85,0x87, //Request for 124800bps. 
0x03,0x27,0x41,0x00,
0x08,0x34,0x40,0xE0,0x00,0x00,0x00,0x04,0x20,
//These are post-loader config strings
0x09,0x36,0xC3,0xC3,0x89,0xC5,0x45,0xDA,0x63,0x0B
};

const char EDC15_Recovery_ArduBytes[] PROGMEM = {
0x83,0x01,0xF1,0x10,0x85,0x87, //Request for 124800bps. 
0x82,0x01,0xF1,0x83,0x00,
0x87,0x01,0xF1,0x83,0x03,0x00,0x50,0x03,0xFF,0x00,
0x03,0x27,0x41,0x00,
0x08,0x34,0x40,0xE0,0x00,0x00,0x00,0x04,0x20,
//These are post-loader config strings
0x09,0x36,0xC3,0xC3,0x89,0xC5,0x45,0xDA,0x63,0x0B
};

const char EDC15P_ArduBytes[] PROGMEM = { 
0x08,0x31,0x02,0x10,0x00,0x00,0x0F,0xBF,0xFF,
0x05,0x23,0x08,0x00,0x00,0x10,
0x05,0x23,0x10,0x00,0x00,0x10,
0x08,0x31,0x02,0x08,0x00,0x00,0x0F,0xBF,0xFF,
};

const char EDC15V_ArduBytes[] PROGMEM = { 
0x08,0x31,0x02,0x08,0x00,0x00,0x0F,0xBF,0xFF
};

const char EDC15VM_ArduBytes[] PROGMEM = { 
0x08,0x31,0x02,0x10,0x00,0x00,0x0F,0xBF,0xFF,
0x05,0x23,0x08,0x00,0x00,0x10,
0x05,0x23,0x10,0x00,0x00,0x10,
0x08,0x31,0x02,0x08,0x00,0x00,0x0F,0xBF,0xFF,
};

//*******************************These are the loaders**************************

//THIS IS THE LOADER THAT IS BEING SENT WHEN WE WANT TO READ/WRITE THE FLASH ON EDC15
//THIS IS THE FIRST BLOCK. FIRST 0xFF MEANS THAT 255 BYTES WILL BE SENT.
const char EDC15_Loader[] PROGMEM = {
0x00, 0xFF, 0x36, 0xA5, 0xA5, 0x14, 0xE0, 0x00, 0x00, 0x3C, 0xE0, 0x00, 
0x00, 0x42, 0xE0, 0x00, 0x00, 0xB0, 0xE3, 0x00, 0x00, 0x02, 0x47, 0x13,
0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 
0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x09, 0x00, 0x0A, 0x00, 0x0B, 0x00,
0x0C, 0x00, 0x0D, 0x00, 0x0E, 0x00, 0x0F, 0x80, 0x0F, 0xA0, 0x0F, 0xC0, 
0x0F, 0x00, 0x10, 0x45, 0x2F, 0x7D, 0x64, 0x9B, 0xC3, 0xBE, 0x88, 0x4F, 
0xD8, 0xE6, 0xF0, 0xFE, 0xE7, 0xDA, 0x00, 0x24, 0xE1, 0x48, 0x61, 0x2D, 
0x39, 0x48, 0x62, 0x2D, 0x16, 0xE6, 0xF1, 0x00, 0xE6, 0xF4, 0x41, 0x01, 
0x00, 0x47, 0xF4, 0x23, 0x00, 0x2D, 0x12, 0x47, 0xF4, 0xA2, 0x00, 0x2D, 
0x12, 0x47, 0xF4, 0xA3, 0x00, 0x2D, 0x17, 0x47, 0xF4, 0xA4, 0x00, 0x2D, 
0x19, 0x47, 0xF4, 0xA5, 0x00, 0x2D, 0x29, 0x47, 0xF4, 0x36, 0x00, 0x2D, 
0x2D, 0xDA, 0x00, 0xE8, 0xE0, 0x0D, 0xE1, 0xDA, 0x00, 0x3E, 0xE2, 0x0D, 
0xDE, 0xE7, 0xFE, 0x55, 0x00, 0xDA, 0x00, 0x88, 0xE1, 0xDA, 0x00, 0xBE, 
0xE2, 0xB7, 0x48, 0xB7, 0xB7, 0xE7, 0xFE, 0x55, 0x00, 0xDA, 0x00, 0x88, 
0xE1, 0x0D, 0xD1, 0xDA, 0x00, 0x06, 0xE1, 0xDA, 0x00, 0xBE, 0xE2, 0xE0, 
0x02, 0xF4, 0x41, 0x02, 0x00, 0xF6, 0xF2, 0xB4, 0xFE, 0xE7, 0xFE, 0xAA, 
0x00, 0xDA, 0x00, 0x88, 0xE1, 0x0D, 0xC3, 0xA9, 0xE1, 0x09, 0xE1, 0xDA, 
0x00, 0x88, 0xE1, 0x0D, 0xBE, 0xDA, 0x00, 0x06, 0xE1, 0xDA, 0x00, 0xE6, 
0xE2, 0xDA, 0x00, 0x06, 0xE1, 0x0D, 0xB7, 0xDA, 0x00, 0xC4, 0xE1, 0xDA, 
0x00, 0x06, 0xE1, 0x0D, 0xB2, 0xDB, 0x00, 0x88, 0x10, 0x88, 0x20, 0xE6, 
0xF1, 0x00, 0xE6, 0xE1, 0x14, 0xB9, 0x41, 0xE7, 0xF4, 0x7F, 0x00, 0xE4, 
0x41, 0x01, 0x00, 0xDA, 0x00, //End of this block, checksum is 0x2D

//SECOND BLOCK
0x00, 0xFF, 0x36, 0x5E, 0xE1, 0x98, 0x20, 0x98, 0x10, 0xDB, 0x00, 0x88, 
0x10, 0x88, 0x20, 0xE6, 0xF1, 0x00, 0xE6, 0xE1, 0x14, 0xB9, 0x41, 0xE7, 
0xF4, 0x76, 0x00, 0xE4, 0x41, 0x01, 0x00, 0xDA, 0x00, 0x5E, 0xE1, 0x98, 
0x20, 0x98, 0x10, 0xDB, 0x00, 0x88, 0x10, 0x88, 0x20, 0x88, 0x30, 0xE6, 
0xF1, 0x00, 0xE6, 0xDA, 0x00, 0x92, 0xE1, 0xE0, 0x02, 0xF1, 0x4E, 0xF1, 
0x6E, 0xB9, 0xE1, 0x08, 0x11, 0xDA, 0x00, 0xA2, 0xE1, 0x48, 0x60, 0x3D, 
0x09, 0xB9, 0xE1, 0x01, 0x6E, 0x08, 0x11, 0xA0, 0x02, 0x3D, 0xF7, 0x21, 
0x6E, 0x41, 0x6E, 0x2D, 0x01, 0xE0, 0x26, 0x98, 0x30, 0x98, 0x20, 0x98, 
0x10, 0xDB, 0x00, 0x88, 0x10, 0x88, 0x20, 0x88, 0x30, 0xE6, 0xF1, 0x00, 
0xE6, 0xE0, 0x02, 0xA9, 0x41, 0xE1, 0x06, 0x99, 0xE1, 0x01, 0x6E, 0xDA, 
0x00, 0x88, 0xE1, 0xA0, 0x02, 0x3D, 0xFA, 0xF1, 0xE6, 0xDA, 0x00, 0x88, 
0xE1, 0x98, 0x30, 0x98, 0x20, 0x98, 0x10, 0xDB, 0x00, 0xF7, 0xFE, 0xB0, 
0xFE, 0xDA, 0x00, 0x92, 0xE1, 0xDB, 0x00, 0xA7, 0x58, 0xA7, 0xA7, 0x9A, 
0xB7, 0xFC, 0x70, 0xF2, 0xF7, 0xB2, 0xFE, 0x7E, 0xB7, 0xDB, 0x00, 0x88, 
0x10, 0xE6, 0xF1, 0xFF, 0xFF, 0xE0, 0x06, 0xA7, 0x58, 0xA7, 0xA7, 0x8A, 
0xB7, 0x04, 0x70, 0xA0, 0x01, 0x3D, 0xFA, 0xE0, 0x16, 0x0D, 0x03, 0xF2,
0xF7, 0xB2, 0xFE, 0x7E, 0xB7, 0x98, 0x10, 0xDB, 0x00, 0x88, 0x10, 0x88, 
0x20, 0x88, 0x30, 0x88, 0x40, 0xE6, 0xF3, 0x00, 0xE6, 0xE0, 0x02, 0xF4, 
0x43, 0x02, 0x00, 0xF4, 0x33, 0x03, 0x00, 0xF4, 0x23, 0x04, 0x00, 0xE0, 
0x04, 0xA9, 0x83, 0x28, 0x44, 0x7C, 0x14, 0x28, 0x41, 0x08, 0x35, 0xA7, 
0x58, 0xA7, 0xA7, 0x99, 0xE3, 0x99, 0xF3, 0xDA, 0x00, 0x08, 0xE2, 0x08,
0x12, 0x18, 0x20, 0xA0, 0x04,
//END OF SECOND BLOCK

//BEGINNING OF THIRD BLOCK
0x00, 0xFF, 0x36, 0x3D, 0xF6, 0x98, 0x40, 0x98, 0x30, 0x98, 0x20, 0x98, 
0x10, 0xDB, 0x00, 0x88, 0x10, 0x88, 0x70, 0xE6, 0xF1, 0xAA, 0xAA, 0xE7, 
0xFE, 0xAA, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xE6, 0xF1, 0x55, 0x55, 0xE7, 
0xFE, 0x55, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xE6, 0xF1, 0xAA, 0xAA, 0xE7, 
0xFE, 0xA0, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0x98, 0x70, 0x98, 0x10, 0xDA, 
0x00, 0x84, 0xE3, 0xDA, 0x00, 0x54, 0xE3, 0xDB, 0x00, 0x88, 0x10, 0x88,
0x20, 0x88, 0x30, 0x88, 0x40, 0x88, 0x50, 0xE6, 0xF3, 0x00, 0xE6, 0xE0, 
0x02, 0xF4, 0x43, 0x02, 0x00, 0xF4, 0x33, 0x03, 0x00, 0xF4, 0x23, 0x04, 
0x00, 0xE0, 0x04, 0xF4, 0x83, 0x05, 0x00, 0x09, 0x81, 0xB9, 0x83, 0x08, 
0x31, 0xE7, 0xFE, 0x36, 0x00, 0xB9, 0xE3, 0x08, 0x31, 0x29, 0x81, 0xDA, 
0x00, 0x90, 0xE2, 0xB9, 0xE3, 0x08, 0x31, 0x08, 0x11, 0x18, 0x20, 0xA0, 
0x04, 0x3D, 0xF8, 0xDA, 0x00, 0x5E, 0xE1, 0x98, 0x50, 0x98, 0x40, 0x98, 
0x30, 0x98, 0x20, 0x98, 0x10, 0xDB, 0x00, 0x88, 0x10, 0xA7, 0x58, 0xA7, 
0xA7, 0xDA, 0x00, 0xA4, 0xE2, 0x66, 0xF1, 0xFF, 0x3F, 0xA9, 0xE1, 0x98, 
0x10, 0xDB, 0x00, 0x88, 0x10, 0x88, 0x20, 0x5C, 0x22, 0x66, 0xF2, 0xFC, 
0x00, 0x1C, 0x21, 0x68, 0x13, 0x70, 0x21, 0xF6, 0xF2, 0x00, 0xFE, 0x98, 
0x20, 0x98, 0x10, 0xDB, 0x00, 0x88, 0x30, 0xE0, 0x03, 0xA7, 0x58, 0xA7, 
0xA7, 0x08, 0x31, 0x46, 0xF3, 0x00, 0x01, 0x3D, 0xFA, 0x98, 0x30, 0xDB, 
0x00, 0x88, 0x30, 0xE0, 0x03, 0xA7, 0x58, 0xA7, 0xA7, 0x08, 0x31, 0x46, 
0xF3, 0x10, 0x00, 0x3D, 0xFA, 0x98, 0x30, 0xDB, 0x00, 0x88, 0x10, 0x88, 
0x20, 0x88, 0x30, 0x88, 0x70, 0xE6, 0xF3, 0x00, 0xE6, 0xE6, 0xF2, 0x20, 
0x00, 0xE6, 0xF1, 0xAA, 0xAA,
//END OF THIRD BLOCK

//BEGINNING OF BLOCK FOUR
0x00, 0xFF, 0x36, 0xE7, 0xFE, 0xAA, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xE6, 
0xF1, 0x55, 0x55, 0xE7, 0xFE, 0x55, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xE6, 
0xF1, 0xAA, 0xAA, 0xE7, 0xFE, 0x80, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xE6, 
0xF1, 0xAA, 0xAA, 0xE7, 0xFE, 0xAA, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xE6, 
0xF1, 0x55, 0x55, 0xE7, 0xFE, 0x55, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xE6, 
0xF1, 0xAA, 0xAA, 0xE7, 0xFE, 0x10, 0x00, 0xDA, 0x00, 0x74, 0xE3, 0xDA, 
0x00, 0xBE, 0xE2, 0xE7, 0xFE, 0x80, 0x00, 0xDA, 0x00, 0x54, 0xE3, 0x98, 
0x70, 0x98, 0x30, 0x98, 0x20, 0x98, 0x10, 0xDB, 0x00, 0x88, 0x20, 0x88, 
0x70, 0x26, 0xF2, 0x18, 0x00, 0xF1, 0xFE, 0x67, 0xFF, 0x80, 0x00, 0xDA, 
0x00, 0x90, 0xE2, 0x67, 0xFE, 0x80, 0x00, 0x41, 0xEF, 0x3D, 0xFA, 0x98, 
0x70, 0x98, 0x20, 0xDB, 0x00, 0x88, 0x10, 0xDA, 0x00, 0xA4, 0xE2, 0x66, 
0xF1, 0xFF, 0x3F, 0xB9, 0xE1, 0x98, 0x10, 0xDB, 0x00, 0x88, 0x10, 0xDA, 
0x00, 0xA4, 0xE2, 0x66, 0xF1, 0xFF, 0x3F, 0xB8, 0x71, 0x98, 0x10, 0xDB, 
0x00, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 
0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0x89, 
0xC5, 0x12, 0xCE, 0xC7, 0xB3, 0xA5, 0xA5, 0x14, 0xE0, 0x00, 0x00, 0x3C, 
0xE0, 0x00, 0x00, 0x42, 0xE0, 0x00, 0x00, 0x00, 0xE4, 0x00, 0x00, 0x02, 
0x47, 0x13, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 
0x05, 0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x09, 0x00, 0x0A, 0x00, 
0x0B, 0x00, 0x0C, 0x00, 0x0D, 0x00, 0x0E, 0x00, 0x0F, 0x80, 0x0F, 0xA0, 
0x0F, 0xC0, 0x0F, 0x00, 0x10, 0x45, 0x2F, 0x7D, 0x64, 0x9B, 0xC3, 0xC3, 
0xC3, 0xC3, 0xC3, 0xC3, 0xC3
//END OF BLOCK FOUR
//END OF LOADER

};


void setup()                    
{
  pinMode(10, OUTPUT);//CS for SD
  pinMode(OKbuttonPin, INPUT);//Set the PB pins
  pinMode(BackbuttonPin, INPUT);
  pinMode(LeftbuttonPin, INPUT);
  pinMode(RightbuttonPin, INPUT);
  pinMode(RXENABLE, OUTPUT);
  digitalWrite(RXENABLE,HIGH);
  lcd.init();
  lcd.backlight();
  pinMode(K_OUT, OUTPUT);
  pinMode(K_IN, INPUT);
  lcd.setCursor(0,0);
  flp("EDC15 tool V0.4");
  lcd.setCursor(0,1);
  flp("Checking SD...");
  //SD card INIT
  if (!SD.begin(SD_CHIP_SELECT, SPI_FULL_SPEED))
  {
    lcd.setCursor(0,3);
    flp("SD Error...");
    fail=1;
    while (fail){}
  }
  lcd.setCursor(14,1);
  flp("Done!");
  delay(1000);
  lcd.clear();
  lcd.setCursor(0,0);
  flp("Select ECU type: ");//if the family selected was EDC15, this menu will show
  optionset = ShowMenuOptions(3,"EDC15P","EDC15V","EDC15VM+");
  delay(500);
  if (optionset == 1)
    {
      EcuType=0;
    }
  if (optionset == 2)
    {
      EcuType=1;
    }
  if (optionset == 3)
    {
      EcuType=2;
    }
       
}



void loop()//Now that we know with which ECU we will work, we show the action menu
{
  VariablesInit();
  serial_rx_off();
  serial_tx_off();
  digitalWrite(K_OUT, HIGH);
   lcd.clear();
   lcd.setCursor(0,0);
   flp("Select operation:");
   byte optionset = ShowMenuOptions(3,"Info","Flash","Tuning");

  if (optionset == 1)//This defines the info reading proccess
    {
      SelectInfo();
    }  
   if (optionset ==2)//Flash operations menu
    {
       lcd.clear();
       lcd.setCursor(0,0);
       flp("Flash operations: ");
       byte optionset = ShowMenuOptions(2,"Read Flash","Write File to flash","");
        
       if (optionset == 0)//Return to main menu
        {
          return;
        }
       if (optionset == 1)//This defines the read proccess
        {
          lcd.clear();
          SelectRead();
        }

       if (optionset == 2)//Write flash proccess
        {
          lcd.clear();
          lcd.setCursor(0,0);
          if (!myFile.open("edc15rd.skf", O_READ))
          {
          // if the file didn't open, print an error:
          flp("File missing in SD");
          delay(2000);
          return;
          }
          SelectWrite();
        }
    }
    
    if (optionset ==3)//Tuning operations menu
    {
       lcd.clear();
       lcd.setCursor(0,0);
       flp("Tuning operations: ");
       byte optionset = ShowMenuOptions(3,"Add file to DB","Write ORI","Write MOD");
        
       if (optionset == 0)//Return to main menu
        {
          return;
        }
       if (optionset == 1)//Add file to the DB
        {
          lcd.clear();
          AddDB(0,0x72000,0x77000,"edc15rd.skf");
          return;
        }

       if (optionset == 2)//Write ORI file
        {
          lcd.clear();
          WriteORI();
        }
        if (optionset == 3)//Write MOD file
        {
          lcd.clear();
          WriteMOD();
        }
    }

  
}


/***********************************Here goes all the operation routines for the menu actions***************/
//***************Read info operations**************//
void SelectInfo()
{
      EDC15CommonStart();
      if (fail)
      {
        lcd.setCursor(0,3);
        flp("Operation failed...");

        while (CheckButtonPressed() == 0)
          {
            
          }
        return;
      }
      ReadEDC15Info();    
}

//This part is done right after the iso_init. Kept only for future auto-detect ECU features.

void ReadEDC15Info()//Searches in flash for the appropiate data string.
{

  ReadEDC15FlashBlock(0x72000,0x77000);
  myFile.open("temp", O_READ);
  long offset=StringSearch("EDC",0x00,0x5000,3);
  if (offset==0)
    {
      offset=StringSearch("TDI",0x00,0x5000,3);
    }
  if (offset==0)
    {
      lcd.clear();
      lcd.setCursor(0,0);
      flp("No data found...");
      lcd.setCursor(0,2);
      delay(5000);
      return ;
    }
    lcd.clear();
    offset=offset-20;
    myFile.seekSet(offset);
    lcd.setCursor(0,0);
    flp("SW: ");//Print ECU version
    for (byte crap=0;crap<11;crap++)
    {
      char asciiconvert= myFile.read();
      lcd.print(asciiconvert);
    }
    offset=offset+12;
    myFile.seekSet(offset);
    lcd.setCursor(0,1);
    flp("Engine: ");
    for (byte crap=0;crap<11;crap++)
    {
      char asciiconvert= myFile.read();
      lcd.print(asciiconvert);
    }
    offset=offset+31;
    myFile.seekSet(offset);
    lcd.setCursor(0,2);
    flp("HW: ");
   for (byte crap=0;crap<11;crap++)
    {
      char asciiconvert= myFile.read();
      lcd.print(asciiconvert);
    }
    lcd.setCursor(0,3);
    flp("ECU SW date: ");
    offset=offset+34;
    myFile.seekSet(offset);
    for (byte crap=0;crap<5;crap++)
    {
      char asciiconvert= myFile.read();
      lcd.print(asciiconvert);
    }
    myFile.remove();
    myFile.close();
    while (CheckButtonPressed() == 0)
    {

    }
    
}
  

//*************Flash operations***************/
void SelectRead()
{
      EDC15CommonStart();
      if (fail)
        {
          lcd.setCursor(0,3);
          flp("Operation failed...");
          delay(2000);
          return;
        }
      lcd.clear();
      lcd.setCursor(0,0);
      flp("Reading flash...");
      ReadEDC15Flash();
      flp("Done");
      delay(1000);   
}

void SelectWrite()
{  
//We need to load the file we will write before getting here
  EDC15CommonStart();
  WriteEDC15Flash();

}

void WriteORI()
{
      EDC15CommonStart();
      for (byte crap=0;crap<1;crap++)
      {
      String peep=CheckInDB();
      if (peep==0)
      {
        return;
      }
      String folder=peep+"/ORI";
      char filename[11];
      folder.toCharArray(filename,12);
      myFile.open(filename, O_READ);
      }
      VariablesInit();
      delay(2000);
      SelectWrite();
}

void WriteMOD()
{
      EDC15CommonStart();
      for (byte crap=0;crap<1;crap++)
      {
      String peep=CheckInDB();
      if (peep==0)
      {
        return;
      }
      String folder=peep+"/MOD";
      char filename[11];
      folder.toCharArray(filename,12);
      if (!myFile.open(filename, O_READ))
      {
        lcd.setCursor(0,3);
        flp("MOD file not found!")
        delay(5000);
        return;
      }
      VariablesInit();
      delay(2000);
      SelectWrite();
}
}

void ReadEDC15FlashBlock(long BlockStart, long BlockEnd)
{
if (myFile.open("temp", O_READ))
  {
  myFile.remove();
  myFile.close();
  }
 if (!myFile.open("temp", O_CREAT | O_WRITE)){
    // if the file didn't open, print an error:
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Error writing to SD");
    lcd.setCursor(0,1);
    flp("Check SD lock");
    delay(5000);
    software_Reset();
   // return;
  }
  long tempstring;
  byte ReadString[7];
  operation=1;
  ReadString[0]= 0x05;
  ReadString[1]=0x23;
  ReadString[5]=0xFE;//First byte indicates the total number of bytes excluding the checksum, and the second byte is the sender ID (the tool)
  //Address is expressed in the third byte starting from 0x08(0x00), and the next two bytes are address too
  boolean finished=0;
  boolean ReadDone=0;  
  do{
    tempstring=(BlockStart>>16)+0x08;
    ReadString[2]=tempstring;
    ReadString[4]=BlockStart&0xFFFF;
    tempstring=BlockStart&0xFFFF;
    ReadString[3]=tempstring>>8;
    ReadString[6]=iso_checksum(ReadString,6);
    iso_write_byte(7,ReadString);//
    ReadEDC15FlashString();
    BlockStart=BlockStart+0xFE;
    if(BlockStart >= BlockEnd)
        {
          myFile.close();
          finished=1;
        }
  }while(!finished);
CloseEDC15Ecu();
}





void ReadEDC15Flash()//Function to read the flash
{ 
 byte before=0;
 if (myFile.open("edc15rd.skf", O_READ))
  {
  myFile.remove();
  myFile.close();
  }
 if (!myFile.open("edc15rd.skf", O_CREAT | O_WRITE)){
    // if the file didn't open, print an error:
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Error writing to SD");
    lcd.setCursor(0,1);
    flp("Check SD lock");
    delay(5000);
    software_Reset();
    //return;
  }
  long ReadStart =0x00;//Last byte before CRC is the lengh of the string you want to receive
  long ReadEnd =0x7FEE2;
  long tempstring;
  byte ReadString[7];
  operation=1;
  ReadString[0]= 0x05;
  ReadString[1]=0x23;
  ReadString[5]=0xFE;//First byte indicates the total number of bytes excluding the checksum, and the second byte is the sender ID (the tool)
  //Address is expressed in the third byte starting from 0x08(0x00), and the next two bytes are address too
  boolean finished=0;
  boolean ReadDone=0;  
  do{
    tempstring=(ReadStart>>16)+0x08;
    ReadString[2]=tempstring;
    ReadString[4]=ReadStart&0xFFFF;
    tempstring=ReadStart&0xFFFF;
    ReadString[3]=tempstring>>8;
    ReadString[6]=iso_checksum(ReadString,6);
    iso_write_byte(7,ReadString);//
    ReadEDC15FlashString();
    if(ReadDone)
        {
         //Here goes the final string
         ReadString[3] = 0xFF;
         ReadString[4] = 0xE0;
         ReadString[5] = 0x20;
         ReadString[6]=iso_checksum(ReadString,6);
         iso_write_byte(7,ReadString);//
          ReadEDC15FlashString();
          myFile.close();
          finished=1;
        }
        ReadStart=ReadStart+0xFE;
      if (ReadStart == ReadEnd)
        {
         ReadDone=1;
        }
   byte percent=(ReadStart*100)/ReadEnd;
   if (percent != before)
   {
     lcd.setCursor(16,0);
     lcd.print(percent);
     flp("%");
     before=percent;
   }
   
  }while(!finished);
CloseEDC15Ecu();
lcd.setCursor(16,0);
}

void ReadEDC15FlashString()//Reads a string of memory sent by the ECU. First byte indicates the total number of bytes, excluding the checksum, that will be received, and the second one is the sender ID(ECU)
{
  byte readbyte[2];
  iso_read_byte();
  readbyte[0]=b;
  CheckRec(0x36);
  readbyte[1]=b;
  SDcounter=0;
  while (SDcounter < (readbyte[0])-1)
  {
    iso_read_byte();
    SDbuffer[SDcounter]=b;
    SDcounter++;
  }
 //Check that the checksum is correct
  iso_read_byte();
  byte checkCRC;
  checkCRC=iso_checksum(readbyte,2);
  checkCRC=checkCRC+iso_checksum(SDbuffer,SDcounter);
  if (checkCRC != b){
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Error: CRC mismatch");
    lcd.setCursor(0,1);
    flp("Expected ");
    lcd.print(checkCRC,HEX);
    lcd.setCursor(0,2);
    flp("Got ");
    lcd.print(b,HEX);
    myFile.close();
      myFile.open("rec.log", O_CREAT | O_WRITE);
      myFile.write(SDbuffer,258);
      myFile.close();
    fail=1;
    while(fail){}
  }
  if (operation==1)
  {
    myFile.write(SDbuffer, SDcounter);
  }
  
} 

void WriteEDC15Flash()
{
  
delay(250);
lcd.clear();
lcd.setCursor(0,0);
flp("Erasing....");
for (byte m=0; m<1; m++)//Send the string, made inside a for to save ram
  {
    byte WriteString[4]={0x02, 0xA5, 0x0F, 0xB6};
    iso_write_byte(4,WriteString);//
  }
  CheckAck();
  //Wait for flash to be erased...
  delay(4000);
  CheckAck();
  flp("Done!");
  lcd.setCursor(0,1);
  flp("Writing boot...");
  WriteEDC15FlashBlock(0x7C000,0x7FFFF);
  flp("Done!");
  lcd.setCursor(0,2);
  flp("Writing flash...");
  WriteEDC15FlashBlock(0x00000,0x7BFFF);
  lcd.setCursor(16,2);
  flp("Done");
  myFile.close();
  CloseEDC15Ecu();
  delay(2000);
}

//Writes a block of data for a range of address
void WriteEDC15FlashBlock(long BlockStart, long BlockEnd)
{
  byte before=0;
  long tempstring;
  myFile.seekSet(BlockStart);
  byte Writebyte[6];
  Writebyte[1]=0x36;
  while (BlockEnd>=BlockStart)
  {
    //extract the numbers from the address counter
    tempstring=(BlockStart>>16)+0x20;
    Writebyte[2]=tempstring;
    tempstring=BlockStart&0xFFFF;
    Writebyte[4]=tempstring;
    Writebyte[3]=tempstring>>8;
    //build the message to be sent
    byte stringcount=0;
    while (BlockEnd>=BlockStart && stringcount <= 0xF9)
    {
     BlockStart++;
     stringcount++;
    }
    myFile.read(SDbuffer,stringcount);
    Writebyte[0]=stringcount+4;//set the header with the length
    Writebyte[5]=iso_checksum(Writebyte,5);//calculate checksumm
    Writebyte[5]=Writebyte[5]+iso_checksum(SDbuffer,stringcount);
    //Thread composed, now need to send it
    iso_write_byte(5,Writebyte);//
    iso_write_byte(stringcount, SDbuffer);//
    byte chk[]={Writebyte[5]};
    iso_write_byte(1,chk);//send the checksumm
     CheckAck();
     if (BlockEnd == 0x7BFFF)
     {
       byte percent=(BlockStart*100)/BlockEnd;
       if (percent != before)
       {
         lcd.setCursor(16,2);
         lcd.print(percent);
         flp("%");
         before=percent;
       }
     }
  }
}

//***********************Loader operations************************//
void SendLoader()
{
  lcd.setCursor(0,2);
  flp("Send Loader...");
  setspeed = 3;
  delay(25);
  SendLoaderString(9,3);
  //Start sending the loader
  for (byte r=0;r<4;r++)
  {
  Send_Loader(257);
  }
  //Loader sent
  iso_sendstring(10);
  CheckAck();
  delayMicroseconds(4510);
  if (EcuType == 0)
  {
    SendEDC15PLoader();
  }
  
  if (EcuType == 1)
  {
    SendEDC15VLoader();
  }
  if (EcuType == 2)
  {
    SendEDC15VMLoader();
  }
  flp("Done!");
}


//This part is for the specific replies that EDC15P ecu gives to the loader
void SendEDC15PLoader()
{
  Specific=1;
  m=0x00;
  w=0x00;
  SendLoaderString(9,4);
  SendLoaderString(6,18);
  SendLoaderString(6,18);
  SendLoaderString(9,2);
}

//This part is for the specific replies that EDC15V ecu gives to the loader
void SendEDC15VLoader()
{
  Specific=1;
  m=0x00;
  w=0x00;
  SendLoaderString(9,4);
}

void SendEDC15VMLoader()
{
  Specific=1;
  m=0x00;
  w=0x00;
  SendLoaderString(9,4);
  SendLoaderString(6,18);
  SendLoaderString(6,18);
  SendLoaderString(9,2);
}

void SendLoaderString(byte a, byte f)
{
  iso_sendstring(a);
  iso_readstring(f);
  delayMicroseconds(4510);
} 


//Sends the EDC15 loader
void Send_Loader(int leng)
{
  int tmpc = 0;
  leng=leng+l;
  while (l<leng)
    {
     b=pgm_read_byte(&EDC15_Loader[l]);
     SDbuffer[tmpc]=b;
     l++;
     tmpc++;
    }
  SDbuffer[tmpc]= 0;
  SDbuffer[tmpc]=iso_checksum(SDbuffer,tmpc);
  tmpc++;
  iso_write_byte(tmpc,SDbuffer);//
  CheckAck();
  delayMicroseconds(4510);
} 

////*****************Seed/Key operations**************//////
void ProcessKey()
{
  long Key1;
  long Key2;
  long Key3 = 0x3800000;
  long tempstring;
  tempstring = SDbuffer[3];
  tempstring = tempstring<<8;
  long KeyRead1 = tempstring+SDbuffer[4];
  tempstring = SDbuffer [5];
  tempstring = tempstring<<8;
  long KeyRead2 = tempstring+SDbuffer[6];
//Process the algorithm 

  if (EcuType == 0)
  {
    Key1=0xDA1C;//EDC15P keys
    Key2=0xF781;
  }
 
  if (EcuType == 1)
  {
    Key1=0x508D;//EDC15V keys
    Key2=0xA647;
  } 
  if (EcuType == 2)
  {
    Key1=0xF25E;//EDC15VM+ keys
    Key2=0x6533;
  } 

 
  for (byte counter=0;counter<5;counter++)
    {
       long temp1;
     long KeyTemp = KeyRead1;
     KeyTemp = KeyTemp&0x8000;
     KeyRead1 = KeyRead1 << 1;
     temp1=KeyTemp&0x0FFFF;
     if (temp1 == 0)
      {
       long temp2 = KeyRead2&0xFFFF;
       long temp3 = KeyTemp&0xFFFF0000;
        KeyTemp = temp2+temp3;
        KeyRead1 = KeyRead1&0xFFFE;
        temp2 = KeyTemp&0xFFFF;
        temp2 = temp2 >> 0x0F;
        KeyTemp = KeyTemp&0xFFFF0000;
        KeyTemp = KeyTemp+temp2;
        KeyRead1 = KeyRead1|KeyTemp;
        KeyRead2 = KeyRead2 << 0x01;
      }

   else

    { 
       long temp2;
       long temp3;
       long temp4;
       KeyTemp = KeyRead2+KeyRead2;
       KeyRead1 = KeyRead1&0xFFFE;
       temp2=KeyTemp&0xFF;
       temp2= temp2|1;
       temp3=Key3&0xFFFFFF00;
       Key3 = temp2+temp3;
       Key3 = Key3&0xFFFF00FF;
       Key3 = Key3|KeyTemp;
       temp2 = KeyRead2&0xFFFF;
       temp3 = KeyTemp&0xFFFF0000;
       KeyTemp = temp2+temp3;
       temp2 = KeyTemp&0xFFFF;
       temp2 = temp2 >> 0x0F;
       KeyTemp = KeyTemp&0xFFFF0000;
       KeyTemp = KeyTemp+temp2;
       KeyTemp = KeyTemp|KeyRead1;
       Key3 = Key1^Key3;
       KeyTemp = Key2^KeyTemp;
       KeyRead2 = Key3;
       KeyRead1 = KeyTemp;
      }
    }
//Done with the key generation 
  KeyRead2=KeyRead2&0xFFFF;//Clean first and secong word from garbage
  KeyRead1=KeyRead1&0xFFFF;
  lcd.setCursor(0,1);
  flp("Bypass auth...");
  SDbuffer[0]=0x06;
  SDbuffer[1]=0x27;
  SDbuffer[2]=0x42;
//Extract the key bytes
  tempstring = KeyRead1;
  SDbuffer[4]=KeyRead1;
  tempstring = tempstring>>8;
  SDbuffer[3]=tempstring;
  tempstring = KeyRead2;
  SDbuffer[6]=KeyRead2;
  tempstring = tempstring>>8;
  SDbuffer[5]=tempstring;
  SDbuffer [7]=iso_checksum(SDbuffer,7);
//done, now send the bytes
  iso_write_byte(8,SDbuffer);
  boolean check=iso_readstring(4);
  if (!check)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Seed auth failed!");
    delay(3000);
    software_Reset();
  }
  flp("Done");
}
  

  
//This is the authentication stage, the seed is requested to the ECU, and then the calculated key is sent
void SeedAuth()
{
  delay(25);   
  iso_sendstring(4);
  for(int s=0; s<8; s++)//read the seed
    {
      iso_read_byte();
      SDbuffer[s] = b;
    }  
  //now we handle the seed bytes
  ProcessKey();
}






//*************ECU wakeup operations****************//


void EDC15CommonStart()
{
  lcd.clear();
  lcd.setCursor(0,0);
  flp("Connecting...");  
  VAGEDC15FastInit();
  flp("Done!");
  SeedAuth();
  SendLoader();
}
//Here goes all the protocol related stuff

//This is the second stage. It is mostly a fast init, speed change to 10400bps and preamble to auth.
void VAGEDC15FastInit()
{
  setspeed = 1;
  delay(100);
  byte counter1=0;
  while (counter1<6 && !ShortBurst())
  {
    counter1++;
    delay(100);
  }
  if (counter1==6)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("No response...");
    delay(2000);
    software_Reset();
  }
  CheckRec(0x83);
  iso_read_byte();
  if (b==0x00)
  {
    NormalInit();
    return;
  }
  if (b==0xF1);
  {
    recovery=1;
    RecoveryInit();
  }
}

void NormalInit()
{
  CheckRec(0x01);
  CheckRec(0x7F);
  CheckRec(0x00);
  CheckRec(0x13);
  CheckRec(0x16);
  delay(75);
  iso_sendstring(5);
  iso_readstring(6);
  iso_readstring(6);
  delay(75);  
  iso_sendstring(4);
  iso_readstring(4);
  delay(75);//after this delay, we could send the string to get the flash chip info
  ShortBurst();
  iso_readstring(6);
  delay(75);
  iso_sendstring(4);
  iso_readstring(4);
  setspeed=3;
  Serial.begin(124800);
}

void RecoveryInit()
{
  lcd.clear();
  lcd.setCursor(0,0);
  flp("Recovery...");
  CheckRec(0x01);
  CheckRec(0xC1);
  CheckRec(0x6B);
  CheckRec(0x8F);
  CheckRec(0x30);
  delay(75);
  iso_sendstring(6);
  iso_readstring(6);//ACK for speed change
  delay(75);
  setspeed = 3;
  Serial.begin(124800);//Should be 124800bps  
  iso_sendstring(5);
  iso_readstring(10);
  delay(75);//after this delay, we could send the string to get the flash chip info
  iso_sendstring(10);
  iso_readstring(5);
  delay(75);
}

boolean ShortBurst()
{
   serial_tx_off(); //disable UART so we can "bit-Bang" the slow init.
   serial_rx_off();
   digitalWrite(K_OUT, LOW);
   delay(25);
   digitalWrite(K_OUT, HIGH);
   serial_rx_on_();
   delay(25);
   byte data[]={0x81,0x01,0xF1,0x81,0xF4};
  iso_write_byte(5,data);// 
  int counter=0;
  while (counter < 400 && Serial.available()<1)
  {
    delay(1);
    counter++;
  }
  if (Serial.available()<1)
  {
    return 0;
  }
  else
  {
    return 1;
  }
  
  
    
}


/**********Response handling operations********/

void CheckAck()//Acknowledge from ECU during write proccess
{
  CheckRec(0x01);
  CheckRec(0x76);
  CheckRec(0x77);
}

void CloseEDC15Ecu()
{
    byte data[]={0x01,0xA2,0xA3};
    iso_write_byte(3,data);//Wave byebye to the ecu
    CheckRec(0x55);
}



//**************Serial ports handling***************//
void serial_rx_on_()
{
  digitalWrite(RXENABLE,LOW);
  if (setspeed == 0)
  {
  Serial.begin(9600);
  }
  if (setspeed == 1)
  {
   Serial.begin(10400);
  } 
    if (setspeed > 1)
  {
   Serial.begin(124800);
  } 
  
}

void serial_rx_off()
{
  digitalWrite(RXENABLE,HIGH);
  UCSR0B &= ~(_BV(RXEN0));  //disable UART RX
}

void serial_tx_off() 
{
   UCSR0B &= ~(_BV(TXEN0));  //disable UART TX
   delay(20);                //allow time for buffers to flush
}

//***********Single byte ISO reading and writing***********//
void iso_read_byte()
{
  int READ_ATTEMPTS=600;
  int readData;
  boolean success = true;
  word t=0;
  b=0;

  while(t != READ_ATTEMPTS  &&  Serial.available() < 1)
  {
    delay(1);
    t++;
  }

  if (t >= READ_ATTEMPTS)
  {
    success = false;
  }

  if (success)
  {
    b = Serial.read();
  }
  else
  {
  }

}

void iso_write_byte(int len, byte data[])
{
  digitalWrite(RXENABLE,HIGH);
  int counter=0;
  while (counter<len)
  {
    Serial.write(data[counter]);
    SendDelay();
    counter++;
  }
  digitalWrite(RXENABLE,LOW);
}

void SendDelay()
{
  if (setspeed == 0)
  {
  delay(ISORequestByteDelay);  // ISO requires 5-20 ms delay between bytes.
  }
  if (setspeed == 1 || setspeed == 2)
  {
    delay(ISORequestByteDelay*2);
  }
    if (setspeed == 3)
  {
    //delayMicroseconds(30);
    delayMicroseconds(65);
  }
}

//********************DB operations**************************//

String CheckInDB()
{
  ReadEDC15FlashBlock(0x72000,0x77000);
  String poopety=AddDB(1,0x00,0x5000,"temp");
  return poopety;
}

 long StringSearch(char content[], long start, long endd, byte leng)
{
  myFile.seekSet(start);
  char filename[leng];
  while (start<endd)//search for the file in the index
    {
      byte counter=0;
      myFile.read(SDbuffer,255);
      while (counter<255)
      {
      char r=SDbuffer[counter];
      if (r==content[0])//if the read byte is the same as the first byte of the name..
      {
        byte issame=0;
        byte crap=1;
        counter++;
        while (issame==0 &&  crap<leng)//read the rest to check
        {
          r=SDbuffer[counter];
          if (r==content[crap])
          {
            crap++;
            counter++;
          }
          else
          { 
            issame=1;
          }
          if (crap==leng)
          {
            return ((start+counter)-leng);
          }
        }
        counter--;
      }
      counter++;
    }
    start=start+255;
    }
    if (start>=endd)
    {
      return 0;
    }
    if (start !=0)
    {
    start=start-leng;
    }
    return start;
}

String AddDB(byte oper,long Start, long End,char source[]) 
{
  lcd.clear();
  lcd.setCursor(0,0);
  if(!myFile.open(source, O_READ))
   {
     flp("File not found in SD");
     lcd.setCursor(0,1);
     flp("Read Flash?");
     delay(3000);
     byte poe=CheckButtonPressed();
     if (poe==2)
      {
        lcd.clear();
        lcd.setCursor(0,2);
        flp("Canceled");
        delay(3000);
        return 0;
      }
      SelectRead();
      myFile.open(source, O_READ);
   }
   char filename[11];
   byte j=11;//determines the char array length
    flp("Search for data...");
    long offset=StringSearch("EDC",Start,End,3);
    if (offset==0)
    {
      offset=StringSearch("TDI",Start,End,3);
      j=10;
    }
    if (offset==0)
    {
      lcd.setCursor(0,1);
      flp("No data found...");
      lcd.setCursor(0,2);
      flp("Default file saved");
      lcd.setCursor(0,3);
      delay(2000);
      return 0;
    }
    if (offset !=0)
    {
      offset=offset-20;
      myFile.seekSet(offset);
      for (byte q=0; q<j; q++)
      {
         filename[q]=myFile.read();
      }
    }
    myFile.close();
    lcd.setCursor(0,1);
    flp("Found: ");
    for (byte crappy=0;crappy<j;crappy++)
    {
       lcd.print(filename[crappy]);
     }
    if (!myFile.open("Index", O_RDWR | O_CREAT))
    {
     lcd.setCursor(0,2); 
     flp("Index not found");
     delay(5000);
     return 0;
    }
    
    String entryfound=IndexSearch(j, filename);//Check in DB 
    if (entryfound==0 && oper==1)//if the entry was not found in the index...
    {
     lcd.clear(); 
      lcd.setCursor(0,0);
      flp("ECU not found in DB");
      lcd.setCursor(0,1);
      flp("Please, add it first");
      myFile.close();
      delay(5000);
      return 0;
    }
    
    if (entryfound==0 && oper==0)//if the entry was not found in the index...
    {
      lcd.setCursor(0,2);
      flp("Entry not found");
      lcd.setCursor(0,3);
      flp("Add to DB?");
      byte poe=CheckButtonPressed();
      if (poe==2)
      {
        lcd.clear();
        lcd.setCursor(6,1);
        flp("Canceled");
        myFile.close();
        delay(2000);
        return 0;
      }
      lcd.clear();
      lcd.setCursor(0,0);
      flp("Adding to DB...");
      int isthere=1;
      byte found=0;
      char crapp[3];
      while (isthere<512 && found == 0)//find what was the last folder created
      {
        itoa(isthere,crapp,10);
        if (!SD.exists(crapp))
          {
            found=1;
          }
        isthere++;
      }
      isthere--;//need to remove 1 as it was added after finding
      for (byte i=0; i<j;i++)
        {
          myFile.print(filename[i]);
        }
      myFile.print("=");
      myFile.print(isthere);
      myFile.println(";");
      myFile.close();
      myFile.open(source, O_READ);
      if (!SD.mkdir(crapp))//create the folder
      {
        flp("SD folder error");
        delay(5000);
        software_Reset();
        //return 0;
      }
      String poo=crapp;///We trim the number we get
      poo.trim();
      poo=poo+"/ORI";
      poo.toCharArray(filename,12);//We convert the string into an array         
      if (!myFile.rename(SD.vwd(),filename))
      {
        flp("SD file error");
        delay(5000);
        software_Reset();
      }
    }
    if (entryfound != 0 && oper==1)//if the entry was found in the index...
      { 
        myFile.close();
        return entryfound;
      }
    
     if (entryfound != 0 && oper==0)//if the entry was found in the index...
      { 
        myFile.close();
        lcd.clear();
        lcd.setCursor(0,0);
        for (byte crapety=0;crapety<1;crapety++)
        {
          String poo=entryfound+"/ORI";
          poo.toCharArray(filename,12);
          if(myFile.open(filename, O_READ))
          {
            flp("ORI found in DB");
          }
          myFile.close();
        }
        lcd.setCursor(0,1);
        for (byte crapety=0;crapety<1;crapety++)
        {
          String poo=entryfound+"/MOD";
          poo.toCharArray(filename,12);
          lcd.setCursor(0,1);
          if(myFile.open(filename, O_READ))
          {
            flp("MOD found in DB");
          }
          else
          {
             flp("MOD not found");
          } 
          myFile.close();
        }
      }
      lcd.setCursor(0,3);
      flp("Done");
      delay(2000);
      return 0;
}
  
String IndexSearch(byte k, char filename[])
{
    String posit;
    byte found=0;
   while (found == 0)//search for the file in the index
    {
      int r=myFile.read();
      if (r<0)
      {
      found=1;
      }
      char s=r;
      
      if (s==filename[0])//if the read byte is the same as the first byte of the name..
      {
        byte issame=0;
        byte crap=1;
        while (issame==0 &&  crap<r)//read the rest to check
        {
          byte tempread=myFile.read();
          if (tempread==filename[crap])
          {
            crap++;
          }
          else
          {
            issame=1;
          }
          if (crap==k)
          {
           char crap=myFile.read();
            crap=myFile.read();
            posit=posit+crap;
            crap=myFile.read();
            while (crap!=';')
            {
              posit=posit+crap;
              crap=myFile.read();
            }
            return posit;
          }
        }
      }
    }
      return 0;
    
}

//******************Read and send strings of data************//

boolean iso_readstring(long leng)
{
  word tmpc = 0;
  byte ff;
  leng = w+leng;
    while (w<leng)
  {
    //EDC15 stuff
    if   (Specific==0 && recovery==0)
    {
      ff=pgm_read_byte(&EDC15_ECUBytes[w]);
    }
    if   (Specific==0 && recovery==1)
    {
      ff=pgm_read_byte(&EDC15_Recovery_ECUBytes[w]);
    }
    
    if   (Specific== 1 && EcuType == 0 )
    {
      ff=pgm_read_byte(&EDC15P_ECUBytes[w]);
    }
    if   (Specific== 1 && EcuType == 1 )
    {
      ff=pgm_read_byte(&EDC15V_ECUBytes[w]);
    } 
        if   (Specific== 1 && EcuType == 2 )//These are for EDC15VM+
    {
      ff=pgm_read_byte(&EDC15VM_ECUBytes[w]);
    } 
  
  if (!CheckRec(ff))
  {
    return 0;
  }
  SDbuffer[tmpc]=ff;
  w++;
  tmpc++;
  }
  SDbuffer [tmpc]= 0;
  SDbuffer [tmpc]=iso_checksum(SDbuffer,tmpc); //Checks that the checksum is correct
  iscrc=1;
  CheckRec(SDbuffer[tmpc]);
  iscrc=0;
  return 1;
}

void iso_sendstring(word leng)//Sends a string stored in flash
{
  digitalWrite(RXENABLE,HIGH);
  word tmpc = 0;
  leng = m+leng;
   while (m<leng)
  {
    //EDC15 stuff
   if   (Specific==0 && recovery==0)
    {
   b=pgm_read_byte(&EDC15ArduBytes[m]);
    }
    
    if   (Specific==0 && recovery==1)
    {
   b=pgm_read_byte(&EDC15_Recovery_ArduBytes[m]);
    }

   if   (Specific== 1 && EcuType == 0 )
    { 
     b=pgm_read_byte(&EDC15P_ArduBytes[m]);   
    }
     if   (Specific== 1 && EcuType == 1 )
    { 
     b=pgm_read_byte(&EDC15V_ArduBytes[m]);  
    }
     if   (Specific== 1 && EcuType == 2 )//These are for EDC15VM+
    { 
     b=pgm_read_byte(&EDC15VM_ArduBytes[m]);  
    }    
   Serial.write(b);
   SDbuffer[tmpc]=b;
   m++;
   tmpc++;
   SendDelay();
  }
  b=iso_checksum(SDbuffer,tmpc);
  Serial.write(b);
  SendDelay();
  digitalWrite(RXENABLE,LOW);
}  


//***************Data handling and checks**************//


//CRC calculation
byte iso_checksum(byte *data, long len)
{
  byte crc=0;
  for(int i=0; i<len; i++)
  {
    crc=crc+data[i];
  }
  return crc;
}




//Checks if the byte we receive is the one we expect
boolean CheckRec(byte p)
{

   iso_read_byte();

   if ( b == 0 && p != 0)
   { 
     iso_read_byte();
   }
     
      if ( b!= p )
   {
     
         if (iscrc == 1)
      {
        flp("CRC doesnt match. ");
        delay(5000);
      software_Reset();
      }
      if (b==0)
      {
      return false;
      }
      if (b!=0)
      {
       lcd.clear();
      lcd.setCursor(0,0);
      flp("Wrong response...");
      lcd.setCursor(0,1);
      flp("Expected ");
      lcd.print(p,HEX);
      lcd.setCursor(0,2);
      flp("Got ");
      lcd.print(b,HEX);
      myFile.close();
      myFile.open("rec.log", O_CREAT | O_WRITE);
      myFile.write(SDbuffer,258);
      myFile.close();
      delay(5000);
      software_Reset();
      return 0;
      }
   }
  return true;
}
 

//********************Menu and buttons handling**************//

byte CheckButtonPressed()
{
  byte nopress=1;
  byte pushedPB;
  while (nopress==1)
  {
  int read1 = digitalRead(OKbuttonPin);
  int read2 = digitalRead(BackbuttonPin);
  int read3 = digitalRead(LeftbuttonPin);
  int read4 = digitalRead(RightbuttonPin);
    if (read1 == HIGH || read2 == HIGH || read3 == HIGH || read4 == HIGH) 
    {
      // reset the debouncing timer
      delay(100);
      
      if (read1== HIGH)
      {
        
        pushedPB=1;//OK
      }
      if (read2== HIGH)
      {
        pushedPB=2;//Back
        
      }
      if (read3== HIGH)
      {
        pushedPB=3;//Left
      }
      if (read4== HIGH)
      {
        pushedPB=4;//Right
      } 
     nopress=0;
    delay(100);     
    }
  }
  return pushedPB;
}
  
byte ShowMenuOptions(byte maxoption,String option1,String option2,String option3)
{
  byte optionshown=0;
  byte pushedPB=0;
  while (pushedPB == 3 || pushedPB == 4 || pushedPB==0)
    {
    if (pushedPB==3)
    {
      optionshown=optionshown-1;
      if (optionshown > maxoption)
      {
        optionshown=maxoption-1;
      }
    }
    if (pushedPB==4)
    {
      optionshown=optionshown+1;
      if (optionshown == maxoption)
      {
        optionshown=0;
      }
    }
    lcd.setCursor(0,2);
    flp("                    ");
      lcd.setCursor(0,2);
      if (optionshown==0)
      {
       lcd.print(option1);
       optionset=1;
      }
      if (optionshown==1)
      {
       lcd.print(option2);
       optionset=2;
      }
      if (optionshown==2)
      {
       lcd.print(option3);
       optionset=3;
      }
      pushedPB=CheckButtonPressed();
  }
  if (pushedPB==1)
  {
  return optionset;
  }
  else
  {
    return 0;
  }
}


//************************Other operations****************//
void VariablesInit()
{
  if (myFile.open("temp", O_READ))
  {
  myFile.remove();
  myFile.close();
  }
  Jobdone=0;
  fail=0;
  setspeed=0;
  Specific=0;
  m=0;
  w=0;
  l=0;
  success = 0;
  iscrc=0;
  recovery=0;
}

void flashprint (const char p[])
{
    byte g;
    while (0 != (g = pgm_read_byte(p++))) {
      char j=g;
      lcd.print(j);
    }
}

void software_Reset() // Restarts program from beginning but does not reset the peripherals and registers
{
asm volatile ("  jmp 0");  
} 


 ///////////////////////////////////
 
 
