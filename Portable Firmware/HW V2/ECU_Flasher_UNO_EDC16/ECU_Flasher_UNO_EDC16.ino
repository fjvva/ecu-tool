/*
EDC15 Toolbox, made by Bi0H4z4rD
It can read/write flash on these ECUS and read their info.
Supports EDC16U31/34
Improved boot time, now the first stage and bitbang are avoided.

TODO:
Need to add retry for packets sent while writing to flash without ACK
Need to add retry on wakeup--Done
Remake the "read info" with the new automatic sw version detection
Adapt the automatic sw version detection to all ecus (not only VAG)
In the definitive hw version, the 33290 CS must be attached to a MCU pin and disabled for programming (LOW)

*/

/*Notes:
-On read address, first goes the start address, then the size of code read

*/



//*Trick to save RAM*////
#define flp(string) flashprint(PSTR(string));
#define RXENABLE 2

//****************LCD stuff****************
#include <Wire_400khz.h> 
#include <LiquidCrystal_I2C.h>
#include <MemoryFree.h>
LiquidCrystal_I2C lcd(0x27,20,4);

 

//This is the push button stuff
const byte OKbuttonPin = 6;
const byte BackbuttonPin = 7;
const byte LeftbuttonPin = 8; 
const byte RightbuttonPin = 9; 

byte FlashType;
/*This determines the type of flash as follows:
type 0=29BL802CB is for ecu type 0
type 1=M58BW016xB is for ecu type 1 and 2
*/

//This is for the menus:
byte optionset=0;

///This is for the key magic
//End of magic variables

/*SD stuff
 * SD card attached to SPI bus as follows:
 ** MOSI - pin 11
 ** MISO - pin 12
 ** CLK - pin 13
 ** CS - pin 10
 */
byte SDbuffer[258];//Buffer to be saved on SD
word SDcounter=0;//counter to know how many bytes will be written to the SD
#include <SdFat.h>
#include <SdFatUtil.h>
SdFat SD;
Sd2Card card; 
SdVolume volume; 
const uint8_t SD_CHIP_SELECT = 10;
SdFile myFile;
SdFile myFile2;
// store error strings in flash to save RAM 
#define error(s) error_P(PSTR(s))



boolean fail=0;//Used to determine wether a proccess has failed, and therefor, return to main menu
#define K_IN    0 //RX
#define K_OUT   1 //TX
#define ISORequestByteDelay 7
byte b=0;
boolean EDC16ReadDone;
long setspeed=10400;
boolean success=0;
byte iscrc=0;
byte EcuType;//determines which ecu is connected
word checksum1=0;
word checksum2=0;

//*****************************These bytes represent the replies that the ECU gives during common parts********************************
//EDC16 stuff
const char EDC16_ECUBytes[] PROGMEM = { //Commonstart
0x83,0xF1,0x10,0xC1,0xEF,0x8F
};

const char Req250k_ECUBytes[] PROGMEM = {
0x83,0xF1,0x10,0x50,0x86,0xA7,//OK TO 250KBPS
};


const char M58BW016xB_Read_ECUBytes[] PROGMEM = {
0x82,0xF1,0x10,0x75,0xFF//READY TO SEND
};

const char M58BW016xB_WriteAck_ECUBytes[] PROGMEM = {
0x82,0xF1,0x10,0x74,0xFD//Write accepted
};

const char M58BW016xB_AddrAck_ECUBytes[] PROGMEM = {
0x82,0xF1,0x10,0x71,0xC4//Address accepted
};

const char Lvl3Sec_ECUBytes[] PROGMEM = {//Answer to correct LVL3 security access
0x83,0xF1,0x10,0x67,0x04,0x34
};

const char Lvl1Sec_ECUBytes[] PROGMEM = {//Answer to correct LVL1 security access
0x83,0xF1,0x10,0x67,0x02,0x34
};

//********************************These bytes represent the replies that Arduino gives during common processes


//EDC16 stuff
const char EDC16ArduBytes[] PROGMEM = { //Commonstart
0x81,0x10,0xF1,0x81
};

const char EDC16Info_ArduBytes[] PROGMEM = { //Request for info
0x82,0x10,0xF1,0x1A,0x80
};

const char Req250k_ArduBytes[] PROGMEM = {
0x83,0x10,0xF1,0x10,0x86,0xA7//REQUEST FOR 250KBPS (A7)
};

const char Req124k_ArduBytes[] PROGMEM = {
0x83,0x10,0xF1,0x10,0x85,0x87//REQUEST FOR 124KBPS (87)
};

const char M58BW016xB_Read_ArduBytes[] PROGMEM = {//ADDRESSING FOR THIS FLASH
0x88,0x10,0xF1,0x35,0x18,0x00,0x00,0x00,0x08,0x00,0x00
};

const char M58BW016xB_Read_Eeprom_ArduBytes[] PROGMEM = {//ADDRESSING FOR THIS FLASH, FULL
0x88,0x10,0xF1,0x35,0x04,0x00,0x00,0x00,0x1C,0x00,0x00
};

const char Lvl3Sec_ArduBytes[] PROGMEM = {//Request for LVL3 security access (Flash read)
0x82,0x10,0xF1,0x27,0x03
};

const char Lvl1Sec_ArduBytes[] PROGMEM = {//Request for LVL3 security access (Flash read)
0x82,0x10,0xF1,0x27,0x01
};

const char EDC16Erase_ArduBytes[] PROGMEM = { //Check if erase is complete
0x82,0x10,0xF1,0x33,0xC4
};
//*******************************Main software**************************





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
  digitalWrite(K_OUT, HIGH);
  lcd.setCursor(0,0);
  flp("EDC16 tool V0.1");
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
  optionset = ShowMenuOptions(3,"EDC16U1","EDC16U31/34","EDC16CP34");
  delay(500);
  if (optionset == 1)
    {
      EcuType=0;
      FlashType=0;
    }
  if (optionset == 2)
    {
      EcuType=1;
      FlashType=1;
    }
  if (optionset == 3)
    {
      EcuType=2;
      FlashType=1;
    }
       
}



void loop()//Now that we know with which ECU we will work, we show the action menu
{
  VariablesInit();
   lcd.clear();
   lcd.setCursor(0,0);
   flp("Select operation:");
   byte optionset = ShowMenuOptions(3,"Info","Flash","Reset Eeprom Counter");

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
          SelectRead(0);

        }

       if (optionset == 2)//Write flash proccess
        {
          lcd.clear();
          SelectWrite();

        }
    }
     if (optionset == 3)//Reset eeprom counter
    {
       lcd.clear();
       lcd.setCursor(0,0);
       flp("Counter operations: ");
       byte optionset = ShowMenuOptions(2,"Enable Reset","Reset Counter","");
        
       if (optionset == 0)//Return to main menu
        {
          return;
        }
       if (optionset == 1)//This defines the read proccess
        {
          lcd.clear();
          PatchEcu();

        }

       if (optionset == 2)//Write flash proccess
        {
          lcd.clear();
          //ResetCounter();

        }
    }  
          
}



/*****************Reset Counter***************/
void PatchEcu()
{
  SelectRead(1);
}


/***********************************Here goes all the operation routines for the menu actions***************/
//***************Read info operations**************//

void SelectInfo()
{
  lcd.clear();
  lcd.setCursor(0,0);
  if (!EDC16CommonStart())
  {
    return;
  }
  if (!LVL3Key())
  {
    return;
  }
  DisplayInfo();
}

void DisplayInfo()
{
  iso_sendstring(5,6);
  while (Serial.available()<1)
  {
  }
  iso_read_byte();
  if (b==0x83)
  {
    delay(1);
     while (Serial.available()>0)
     {
       iso_read_byte();
       delay(1);
     }
    while (Serial.available()<1)
  {
  }
  iso_read_byte();
  }

  SDbuffer[0]=b;
  int crap=1;
  delay(1);
  while (Serial.available()>0)
  {
  iso_read_byte();
  SDbuffer[crap]=b;
  crap++;
  delay(1);
  }
  CloseEDC16Ecu();
  //32-33 and 35-36 are the sw date
  //79-95 are the VIN
  //131-142 are the SW version
  //144-147 are the SW revision
  //158-168 is the engine type
    lcd.clear();
    lcd.setCursor(0,0);
    flp("SW: ");//Print ECU version
    for (byte crap=87;crap<98;crap++)
    {
      char asciiconvert= SDbuffer[crap];
      lcd.print(asciiconvert);
    }
    lcd.setCursor(0,1);
    flp("Engine: ");
    for (byte crap=147;crap<158;crap++)
    {
      char asciiconvert= SDbuffer[crap];
      lcd.print(asciiconvert);
    }
    lcd.setCursor(0,2);
    flp("VIN: ");
   for (byte crap=73;crap<84;crap++)
    {
      char asciiconvert= SDbuffer[crap];
      lcd.print(asciiconvert);
    }
    lcd.setCursor(0,3);
    flp("ECU SW date: ");
    for (byte crap=32;crap<37;crap++)
    {
      char asciiconvert= SDbuffer[crap];
      lcd.print(asciiconvert);
    }
    while (CheckButtonPressed() == 0)
    {

    }
  
}
//*************Flash operations***************/


void SelectRead(byte op)
{
  lcd.clear();
  lcd.setCursor(0,0);
  boolean check=EDC16CommonStart();
  if (!check)
  {
    return;
  }
  check=LVL3Key();
  if (!check)
  {
    return;
  }
  EDC16ReadStart(op);
  ReadEDC16Flash(op);
  delay(1000);    
}

void SelectWrite()
{
  lcd.clear();
  lcd.setCursor(0,0);
  //PrepareFile();
  lcd.clear();
  lcd.setCursor(0,0);
  if (!SlowInit())
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("No response!");
    delay(2000);
    return;
  }
  if (!LVL1Key())
  {
    return;
  }
  SetSpeed();
  lcd.clear();
  lcd.setCursor(0,0);
  flp("Will Write 2 blocks");
  lcd.setCursor(0,1);
  flp("Block 1...");
  EDC16WriteBlock("EDC16RD.CR2",7,1,1);//we write block 7 (0x1C0000-0x1FFFFF)
  lcd.setCursor(0,2);
  flp("Block 2...");
  EDC16WriteBlock("EDC16RD.CR1",6,2,2);//then block 6
  lcd.setCursor(0,3);
  flp("Done!");
  CloseEDC16Ecu();
  delay(2000);  
}

void EDC16WriteBlock(char filename[],byte blockno,byte pos,byte crc)
{
 //We send the block start and size to be written 
 if (!myFile.open(filename, O_READ))
     {//We do it the fast way
      // if the file didn't open, print an error:
      lcd.clear();
      lcd.setCursor(0,0);
      flp("SD card error");
      lcd.setCursor(0,1);
      lcd.print(filename);
      return;
    }
 SendAddress(blockno);
 //We send the erase command
 SendErase(blockno);
 while (!CheckErase())//wait until flash is erased...
 {
   delay(100);
 }
 
 WriteEDC16FlashBlock(blockno,filename,pos);
 myFile.close();
 FinishWrite(blockno, crc);
}
 
void FinishWrite(byte blockno, byte crc)
{
  delay(75);
  SDbuffer[0]=0x81;
  SDbuffer[1]=0x10;
  SDbuffer[2]=0xF1;
  SDbuffer[3]=0x37;
  WriteString(4);
  while(Serial.available()<1)
  {}
  CheckRec(0x81);
  CheckRec(0xF1);
  CheckRec(0x10);
  CheckRec(0x77);
  CheckRec(0xF9);
  delay(75);
  for (byte crap=0;crap<2;crap++)
  {
  SDbuffer[0]=0x81;//send tester present
  SDbuffer[1]=0x10;
  SDbuffer[2]=0xF1;
  SDbuffer[3]=0x3E;
  WriteString(4);
  CheckRec(0x81);
  CheckRec(0xF1);
  CheckRec(0x10);
  CheckRec(0x7E);
  CheckRec(0x00);
  delay(75);
  } 
  
  SDbuffer[0]=0x8A;
  SDbuffer[1]=0x10;
  SDbuffer[2]=0xF1;
  SDbuffer[3]=0x31;
  SDbuffer[4]=0xC5;
  SDbuffer[5]=blockno*4;
  SDbuffer[6]=0x00;
  SDbuffer[7]=0x00;
  blockno++;
  SDbuffer[8]=blockno*4;
  SDbuffer[8]--;
  if (blockno==8)//must be incremented by one
  {
  SDbuffer[9]=0xDF;
  }
  else 
  {
    SDbuffer[9]=0xFF;
  }
  SDbuffer[10]=0xFF;
  if (crc==1)
  {
    b=checksum1>>8;
  }
  if (crc==2)
  {
    b=checksum2>>8;
  }
  SDbuffer[11]=b;//These two bytes are the file checksumm
  if (crc==1)
  {
    b=checksum1;
  }
  if (crc==2)
  {
    b=checksum2;
  }
  SDbuffer[12]=b;
  delay(75);
  WriteString(13);
  CheckRec(0x82);
  CheckRec(0xF1);
  CheckRec(0x10);
  CheckRec(0x71);
  CheckRec(0xC5);
  CheckRec(0xB9);
  FinalCheck(); 
}
 
void FinalCheck()
{
  delay(75);
  boolean crap=0;
  while (!crap)
  {
    SDbuffer[0]=0x82;
    SDbuffer[1]=0x10;
    SDbuffer[2]=0xF1;
    SDbuffer[3]=0x33;
    SDbuffer[4]=0xC5;
    WriteString(5);
    ReadString();
    if (SDbuffer[3]!=0x7F)
    {
      crap=1;
    }
    delay(100);
  }
  
}
    
 
 
 
//Writes a block of data for a range of address
void WriteEDC16FlashBlock(byte blockno, char filename[],byte pos)
{
  long BlockStart=0;
  long BlockEnd;
  if (blockno==7)
  {
   BlockEnd=0x3E000;
  }
  else 
  {
    BlockEnd=0x40000;
  }
  blockno++;
  byte before=0;
  byte Writebyte[4];
  Writebyte[0]=0x00;
  Writebyte[2]=0x36;
  while (BlockEnd>BlockStart)
  {
    //build the message to be sent
    byte stringcount=0;
    while (BlockEnd>BlockStart && stringcount <= 0xF8)
    {
     BlockStart++;
     stringcount++;
    }
    myFile.read(SDbuffer,stringcount);
    Writebyte[1]=stringcount+1;//set the header with the length
    Writebyte[3]=iso_checksum(Writebyte,3);//calculate checksumm
    Writebyte[3]=Writebyte[3]+iso_checksum(SDbuffer,stringcount);
    //Thread composed, now need to send it
    digitalWrite(RXENABLE,HIGH);
    for (byte sendcount=0; sendcount < 3; sendcount++)//send the header
    {
      Serial.write(Writebyte[sendcount]);
      SendDelay();
    } 
    long totaldelay=100;
    for (byte sendcount=0; sendcount < stringcount; sendcount++)//send the data
    {
       Serial.write(SDbuffer[sendcount]);
       totaldelay=totaldelay+25;
       SendDelay();
     }
     Serial.write(Writebyte[3]);//send the checksumm
     delayMicroseconds(totaldelay);
     digitalWrite(RXENABLE,LOW);
     CheckAck();

       byte percent=(BlockStart*100)/BlockEnd;
       if (percent != before)
       {
         lcd.setCursor(16,pos);
         lcd.print(percent);
         flp("%");
         before=percent;
       }    
  }

} 
 

boolean CheckErase()
{
 iso_sendstring(5,8);
 if (Serial.available() != 0)
 {
   flp("BUG!");
   lcd.setCursor(0,0);
   while (Serial.available()!=0)
   {
   b=Serial.read();
   lcd.print(b,HEX);
   }
 while(1){}
 }

   
 while (Serial.available()<1)
  {
  }
  byte crap=0;
  delay(1);
 while (Serial.available()>0)
   {
     iso_read_byte();
     SDbuffer[crap]=b;
     crap++;
     delay(1);
    }
  if (SDbuffer[3]==0x7F)
   {
    return 0;
   }
  else
   {
    return 1;
   }
} 
 

void EDC16ReadStart(byte op)
{
  delay(75);
  SetSpeed();
  delay(75);
  if (FlashType==0)
  {
  }
  if (FlashType==1)
  {
   if (op==0)
   {
    iso_sendstring(11,2);
   }
   if (op==1)
   {
     iso_sendstring(11,7);
   }
    iso_readstring(5,2);
  }
}



void ReadEDC16Flash(byte op)
{
  if (op==0)
  {
      if (myFile.open("edc16rd.skf", O_READ))
              {
                myFile.remove();
                myFile.close();
               }
    if (!myFile.open("EDC16RD.skf", O_CREAT | O_WRITE))
     {//We do it the fast way
      // if the file didn't open, print an error:
      lcd.clear();
      lcd.setCursor(0,0);
      flp("SD card error");
      return;
    }
  }
  if (op==1)
  {
      if (myFile.open("FULL.skf", O_READ))
              {
                myFile.remove();
                myFile.close();
               }
    if (!myFile.open("FULL.skf", O_CREAT | O_WRITE))
     {//We do it the fast way
      // if the file didn't open, print an error:
      lcd.clear();
      lcd.setCursor(0,0);
      flp("SD card error");
      return;
    }
    for (int crap=0;crap<256;crap++)
    {
      SDbuffer[crap]=0xFF;
    }
    for (long crap=0;crap<0x3FFFF;crap=crap+256)
    {
      myFile.write(SDbuffer,256);
    }
  }
  lcd.clear();
  lcd.setCursor(0,0);
  flp("Reading Flash...");
  byte before=0;
  long start=0;
  long End;
  
  if (op==0)
  {
  End=0x7FFFF;
  }
  
  if (op==1)
  {
  End=0x1C0000;
  }
  
  while (!EDC16ReadDone)
  {
  EDC16ReadRequest();
  EDC16ReadString();
  start=start+254;
  byte percent=(start*100)/End;
  if (percent != before)
  {
   lcd.setCursor(16,0);
   lcd.print(percent);
   flp("%");
   before=percent;
   }
  }
  flp("Done");
  myFile.close();
  CloseEDC16Ecu();
  
}


void EDC16ReadString()
{
  byte header[5];
  CheckRec(0x80);//We store the header to be able to proccess the checksumm later
  header[0]=b;
  CheckRec(0xF1);
  header[1]=b;
  CheckRec(0x10);
  header[2]=b;
  iso_read_byte();
  header[3]=b;
  if (b!=0xFF)//If the length of the packet is less than 0xFF...
  {
    EDC16ReadDone=1;//let the function know that it is the last packet that ECU will send over
  }
  CheckRec(0x76);
  header[4]=b;
  SDcounter=0;
  while (SDcounter < (header[3]-1))
  {
    iso_read_byte();
    SDbuffer[SDcounter]=b;
    SDcounter++;
  }
  
  
 //Check that the checksum is correct
  iso_read_byte();
  byte checkCRC;
  checkCRC=iso_checksum(SDbuffer,SDcounter);
  checkCRC=checkCRC+iso_checksum(header,5);
  if (checkCRC != b)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("CRC error, got ");
    lcd.print(b,HEX);
    lcd.setCursor(0,1);
    flp("Expected ");
    lcd.print(checkCRC,HEX);
    fail=1;
    while(fail){}
  }
  myFile.write(SDbuffer, SDcounter);
}


void EDC16ReadRequest()//Sends a request for a flash data packet
{ 
  SDbuffer[0]=0x80;//Send the flash packet request
  SDbuffer[1]=0x10;
  SDbuffer[2]=0xF1;
  SDbuffer[3]=0x01;
  SDbuffer[4]=0x36;
  WriteString(5);
}

//*******************Memory addressing*************/

void SendAddress(byte blockno)// this function is checked
{
 SDbuffer[0]=0x88;
 SDbuffer[1]=0x10;
 SDbuffer[2]=0xF1;
 SDbuffer[3]=0x34;//This is the address command
 SDbuffer[4]=blockno*4;//Start address
 SDbuffer[5]=0x00;
 SDbuffer[6]=0x00;
 SDbuffer[7]=0x02;
 if (blockno==7)
 {
  SDbuffer[8]=0x03;//This indicates the length of the block that will be written (256kb)
  SDbuffer[9]=0xE0;
  SDbuffer[10]=0x00; 
 }
 else
 {
 SDbuffer[8]=0x04;//This indicates the length of the block that will be written (256kb)
 SDbuffer[9]=0x00;
 SDbuffer[10]=0x00;
 }
 delay(75);
 WriteString(11);
 iso_readstring(5,6);//check the ACK
}


void SendErase(byte blockno)//this function is confirmed
{
 SDbuffer[0]=0x8E;
 SDbuffer[1]=0x10;
 SDbuffer[2]=0xF1;
 SDbuffer[3]=0x31;
 SDbuffer[4]=0xC4;
 SDbuffer[5]=blockno*4;
 SDbuffer[6]=0x00;
 SDbuffer[7]=0x00;
 byte endblock=blockno+1;
 SDbuffer[8]=endblock*4;
 SDbuffer[8]--;
 SDbuffer[9]=0xFF;
 SDbuffer[10]=0xFF;
 SDbuffer[11]=0x00;
 SDbuffer[12]=0x00;
 SDbuffer[13]=0x00;
 SDbuffer[14]=0x00;
 SDbuffer[15]=0x18;//these are static independent from the block length
 SDbuffer[16]=0xB5;
 delay(75);
 WriteString(17);
 iso_readstring(5,7);//check the ACK
 delay(5);
}

/***************Speed handling************///


void SetSpeed()
{
  if (!Set250kSpeed())
  {
    Set124kSpeed();
  }
}

boolean Set124kSpeed()
{
  delay(75);
  iso_sendstring(6,9);//change speed to 124kbps
  ReadString();
  
  if (SDbuffer[3]==0x7F)
  {
    ReadString();   
  }
  delay(75);
  setspeed=124800;
  Serial.begin(setspeed);
  return 1;
}


boolean Set250kSpeed()
{
  delay(75);
  iso_sendstring(6,3);//change speed to 250kbps
  ReadString();
  if (SDbuffer[3]==0x7F)
  {
    return 0;
  }
  delay(75);
  setspeed=250000;
  Serial.begin(setspeed);
  return 1;
}


/***************File handling operations**************/

void PrepareFile()
{
  flp("Process RSA...");
  if (!myFile.open("EDC16RD.skf", O_READ)){//We do it the fast way
    lcd.setCursor(0,1);
    flp("Source file Error");
  }
  if (myFile2.open("EDC16RD.CR2", O_READ))
  {
  myFile2.remove();
  myFile2.close();
  }
  if (myFile2.open("EDC16RD.CR1", O_READ))
  {
  myFile2.remove();
  myFile2.close();
  }
  //Encrypt first block
  myFile2.open("EDC16RD.CR1", O_CREAT | O_WRITE);
  checksum2=Encrypt(0x0000,0x40000);
  myFile2.open("EDC16RD.CR2", O_CREAT | O_WRITE);
  checksum1=Encrypt(0x40000,0x7E000);
  flp("Done!");
  delay(1000);
  myFile.close();
  }

word Encrypt(long start, long finish)
{
  word checksum=0;
  long EAX=0x10000;
  long ECX=0x27C0020;
  long EDX=0x3FE45D9A;
  long EBX=0x0;
  long ESP=0x12E794;
  byte EBP=0x3;
  long EDI=0x10000;
  myFile.seekSet(start);
  int counter=0;
  byte buff[128];
  byte buffcount=0;
  while (start<finish)
  {
    EAX=EDX;
    ECX=EDX;
    EAX=EAX>>20;
    EAX=EAX&0x400;
    ECX=ECX&0x400;
    EAX=EAX^ECX;
    ECX=EDX;
    ECX=ECX>>31;
    EAX=EAX>>10;
    ECX=ECX&0x01;
    EBX=EDX;
    EAX=EAX^ECX;
    ECX=EDX;
    EBX=EBX&0x01;
    ECX=ECX>>1;
    EBX=EBX^EAX;
    if (EBX ==0)
    {
      EDI=EDI&0xFFFFFFFE;
    }   
    if (EBX !=0)
    {
      EDI=EDI|0x01;
    }
    EAX=0;
    EDX=EDI;
    EDX=EDX&0x01;
    EDX=EDX|EAX;
    if (EDX ==0)
    {
      ECX=ECX&0x7FFFFFFF;
    }
    if (EDX !=0)
    {
      ECX=ECX|0x80000000;
    }
    EBP--;
    EDX=ECX;
    if (EBP ==0)
    {
      if (buffcount==0)
      {
      myFile.read(buff,128);
      }
      EAX=buff[buffcount];
      checksum=checksum+EAX;
      buffcount++;
      byte a=EAX&0xFF;
      byte b=ECX&0xFF;
      a=a^b;
      SDbuffer[counter]=a;
      counter++;
      start++;
      byte c=buff[buffcount];
      checksum=checksum+c;
      buffcount++;
      EBX=EBX&0xFFFFFF00;
      EBX=EBX+c;
      EAX=ECX;
      EAX=EAX>>8;
      a=EAX&0xFF;
      b=EBX&0xFF;
      a=a^b;
      EBX=EBX&0xFFFFFF00;
      EBX=EBX+a;
      EAX=ECX;
      a=EBX&0xFF;
      SDbuffer[counter]=a;
      counter++;
      start++;
      c=buff[buffcount];
      checksum=checksum+c;
      buffcount++;
      EBX=EBX&0xFFFFFF00;
      EBX=EBX+c;
      EAX=EAX>>0x10;
      a=EAX&0xFF;
      b=EBX&0xFF;
      a=a^b;
      EBX=EBX&0xFFFFFF00;
      EBX=EBX+a;
      EAX=0x10000;
      a=EBX&0xFF;
      SDbuffer[counter]=a;
      counter++;
      start++;
      c=buff[buffcount];
      checksum=checksum+c;
      buffcount++;
      EBX=EBX&0xFFFFFF00;
      EBX=EBX+c;
      ECX=ECX>>0x18;
      a=ECX&0xFF;
      b=EBX&0xFF;
      a=a^b;
      EBX=EBX&0xFFFFFF00;
      EBX=EBX+a;
      EAX--;
      a=EBX&0xFF;
      SDbuffer[counter]=a;
      counter++;
      start++;
      if (counter>=254)
      {
        myFile2.write(SDbuffer,counter);
        counter=0;
      }
      EBP=3;
      if (buffcount==128)
      {
        buffcount=0;
      }
    }
  }

  myFile2.close();
  return checksum;
}

////*****************Seed/Key operations**************//////

  

  
//This is the authentication stage, the seed is requested to the ECU, and then the calculated key is sent


boolean LVL3Key()
{
  lcd.setCursor(0,1);
  flp("Bypass auth...");
  delay(25);
  iso_sendstring(5,4);//Request LVL3 security access
  for(byte s=0; s<10; s++)
    {
      iso_read_byte();
      SDbuffer[s]=b;
    }
  b=iso_checksum(SDbuffer,9);
  if (b !=SDbuffer[9])
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Seed CRC mismatch!");
    delay(2000);
    return 0;
  }
  long tempstring;
  tempstring = SDbuffer [5];
  tempstring = tempstring<<8;
  long KeyRead1 = tempstring+SDbuffer[6];
  tempstring = SDbuffer [7];
  tempstring = tempstring<<8;
  long KeyRead2 = tempstring+SDbuffer[8];
  KeyRead1=KeyRead1<<16;
  KeyRead1=KeyRead1+KeyRead2;
  if (EcuType==1)
    {
      KeyRead1=KeyRead1+0x2FC9;
    }
  SDbuffer[0]=0x86;
  SDbuffer[1]=0x10;
  SDbuffer[2]=0xF1;
  SDbuffer[3]=0x27;
  SDbuffer[4]=0x04;
//Extract the key bytes
  SDbuffer[8]=KeyRead1;
  KeyRead1 = KeyRead1>>8;
  SDbuffer[7]=KeyRead1;
  KeyRead1 = KeyRead1>>8;
  SDbuffer[6]=KeyRead1;
  KeyRead1 = KeyRead1>>8;
  SDbuffer[5]=KeyRead1;
//done, now send the bytes
  delay(25);
  WriteString(9);
  boolean check =iso_readstring(6,4);
  if (!check)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Seed auth failed!");
    delay(2000);
    return 0;
  }
 flp("Done!");
return 1; 
}


boolean LVL1Key()

//Aternative keys, not yet know for what ecu: 5D5B 5FBD
{
  lcd.setCursor(0,1);
  flp("Bypass auth...");    
    delay(25);   
    iso_sendstring(5,5);//request LVL1 security access
    for(byte s=0; s<10; s++)
    {
      iso_read_byte();
      SDbuffer[s] = b;
    }
    b=iso_checksum(SDbuffer,9);
  if (b !=SDbuffer[9])
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Seed CRC mismatch!");
    delay(2000);
    return 0;
  }
  //now we handle the seed bytes 
  long tempstring;
  tempstring = SDbuffer [5];
  tempstring = tempstring<<8;
  long KeyRead1 = tempstring+SDbuffer[6];
  tempstring = SDbuffer [7];
  tempstring = tempstring<<8;
  long KeyRead2 = tempstring+SDbuffer[8];
  byte counter=0;
  long Magic1 = 0x1C60020;
  while (counter<5)
    {
     long temp1;
     tempstring = KeyRead1;
     tempstring = tempstring&0x8000;
     KeyRead1 = KeyRead1 << 1;
     temp1=tempstring&0xFFFF;//Same as EDC15 until this point
      if (temp1 == 0)//this part is the same for EDC15 and EDC16
       {
          long temp2 = KeyRead2&0xFFFF;
          long temp3 = tempstring&0xFFFF0000;
          tempstring = temp2+temp3;
          KeyRead1 = KeyRead1&0xFFFE;
          temp2 = tempstring&0xFFFF;
          temp2 = temp2 >> 0x0F;
          tempstring = tempstring&0xFFFF0000;
          tempstring = tempstring+temp2;
          KeyRead1 = KeyRead1|tempstring;
          KeyRead2 = KeyRead2 << 1;
       }

     else
      { 
         long temp2;
         long temp3;
         tempstring = KeyRead2+KeyRead2;
         KeyRead1 = KeyRead1&0xFFFE;
         temp2=tempstring&0xFF;//Same as EDC15 until this point
         temp3=Magic1&0xFFFFFF00;
         temp2= temp2|1;
         Magic1=temp2+temp3;
         Magic1 = Magic1&0xFFFF00FF;
         Magic1 = Magic1|tempstring;
         temp2=KeyRead2&0xFFFF;
         temp3=tempstring&0xFFFF0000;
         temp2=temp2 >> 0x0F;
         tempstring=temp2+temp3;
         tempstring=tempstring|KeyRead1;
         Magic1=Magic1^0x1289;
         tempstring=tempstring^0x0A22;
         KeyRead2=Magic1;
         KeyRead1=tempstring;
      }
  
  counter++;
 }
  SDbuffer[0]=0x86;
  SDbuffer[1]=0x10;
  SDbuffer[2]=0xF1;
  SDbuffer[3]=0x27;
  SDbuffer[4]=0x02;
//Extract the key bytes
  SDbuffer[6]=KeyRead1;
  KeyRead1 = KeyRead1>>8;
  SDbuffer[5]=KeyRead1;
  SDbuffer[8]=KeyRead2;
  KeyRead2 = KeyRead2>>8;
  SDbuffer[7]=KeyRead2;
//done, now send the bytes
  delay(25);
  WriteString(9);
  boolean check=iso_readstring(6,5);//Ack for correct key sent
  if (!check)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    flp("Seed auth failed!");
    delay(2000);
    return 0;
  }
 flp("Done!");
 return 1; 
}
  
  
  
  


//*************ECU wakeup operations****************//

boolean EDC16CommonStart()
{
      lcd.clear();
      lcd.setCursor(0,0);
      flp("Connecting...");
      setspeed=10400;
      byte countercrap=0;
      boolean pitipoop=0;
      while (!pitipoop && countercrap<20)
      {
        pitipoop=ShortBurst();
        countercrap++;
      }
      if (countercrap==20)
      {
        lcd.clear();
        lcd.setCursor(0,0);
        flp("No response!");
        delay(2000);
        return 0;
      }
      flp("Done!");
      return 1;

}

boolean Bitbang()
{
  lcd.clear();
  lcd.setCursor(0,0);
  flp("Connecting...");
  serial_tx_off(); //Send address 0x01@5baud
  serial_rx_off();
  digitalWrite(K_OUT, HIGH);
  delay(300);
  digitalWrite(K_OUT, LOW);
  delay(200);
  digitalWrite(K_OUT, HIGH);
  delay(200);
  digitalWrite(K_OUT, LOW);
  delay(1400);
  digitalWrite(K_OUT, HIGH);
  setspeed=10400;
  serial_rx_on_();
  if (!CheckRec(0x55))
  {
    return 0;
  }
  return 1;
  
} 

boolean SlowInit()
{
  lcd.clear();
  lcd.setCursor(0,0);
  flp("Connecting...");
  byte counter=0;
      boolean pitipoop=0;
      while (!pitipoop && counter<6)
      {
        pitipoop=Bitbang();
        counter++;
      }
  if (counter==6)
  {
    return 0;
  }
  iso_read_byte();
  iso_read_byte();
  delay(45);
  digitalWrite(RXENABLE,HIGH);
  Serial.write(~b);
  delay(1);
  digitalWrite(RXENABLE,LOW);
  CheckRec(0xFE);
  delay(45);
  iso_sendstring(4,1);
  iso_readstring(6,1);
  flp("Done!");
  return 1;
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
   iso_sendstring(4,1);
   byte counter=0;
   while (counter < 200 && Serial.available()<1)
  {
    delay(1);
    counter++;
  }
  if (Serial.available()<1)
  {
    return 0;
  }
  iso_readstring(6,1);
  delay(75); 
  return 1;
}

/**********Response handling operations********/

void CheckAck()//Acknowledge from ECU during write proccess
{
  CheckRec(0x00);
  CheckRec(0x01);
  CheckRec(0x76);
  CheckRec(0x77);
}

void CloseEDC16Ecu()
{
  delay(10);
  SDbuffer[0]=0x81;
  SDbuffer[1]=0x10;
  SDbuffer[2]=0xF1;
  SDbuffer[3]=0x82;
  WriteString(4);
  CheckRec(0x81);
  CheckRec(0xF1);
  CheckRec(0x10);
  CheckRec(0xC2);
  CheckRec(0x44);
}


//**************Serial ports handling***************//
void serial_rx_on_()
{
  Serial.begin(setspeed);
  digitalWrite(RXENABLE,LOW);
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
  boolean success = true;
  int t=0;
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

}

void SendDelay()
{
  if (setspeed == 10400)
  {
  delay(ISORequestByteDelay);  // ISO requires 5-20 ms delay between bytes.
  }
    else
  {
    delayMicroseconds(3);
    //delayMicroseconds(30);
  }
}

//******************Read and send strings of data************//

boolean iso_readstring(byte leng, byte op)
{
  byte tmpc = 0;
  byte ff;

  for (tmpc=0;tmpc<leng;tmpc++)
  {
   
   if (op==1)//Commonstart
   {
     ff=pgm_read_byte(&EDC16_ECUBytes[tmpc]); 
   }
   if (op==2)
   {
     ff=pgm_read_byte(&M58BW016xB_Read_ECUBytes[tmpc]);
   }
   if (op==3)
   {
     ff=pgm_read_byte(&Req250k_ECUBytes[tmpc]);
   }
   if (op==4)
   {
     ff=pgm_read_byte(&Lvl3Sec_ECUBytes[tmpc]);
   }
   if (op==5)
   {
     ff=pgm_read_byte(&Lvl1Sec_ECUBytes[tmpc]);
   }
   if (op==6)
   {
     ff=pgm_read_byte(&M58BW016xB_WriteAck_ECUBytes[tmpc]);
   }
   if (op==7)
   {
     ff=pgm_read_byte(&M58BW016xB_AddrAck_ECUBytes[tmpc]);
   }
  CheckRec(ff);
  SDbuffer[tmpc]=ff;
  }
  SDbuffer [tmpc]=iso_checksum(SDbuffer,tmpc); //Checks that the checksum is correct
  iscrc=1;
  boolean check= CheckRec(SDbuffer[tmpc]);
  if (!check)
  {
    iscrc=0;
    return 0;
  }
  iscrc=0;
  return 1;
}

void iso_sendstring(byte leng, byte op)//Sends a string stored in flash
{
  long totaldelay=52;
  digitalWrite(RXENABLE,HIGH);
  byte tmpc = 0;
  for (tmpc=0;tmpc<leng;tmpc++)
  {
   if (op==1)//Commonstart
   {
     b=pgm_read_byte(&EDC16ArduBytes[tmpc]); 
   }
   if (op==2)
   {
     b=pgm_read_byte(&M58BW016xB_Read_ArduBytes[tmpc]);
   }
   if (op==3)
   {
     b=pgm_read_byte(&Req250k_ArduBytes[tmpc]);
   }
   if (op==4)
   {
     b=pgm_read_byte(&Lvl3Sec_ArduBytes[tmpc]);
   }
   if (op==5)
   {
     b=pgm_read_byte(&Lvl1Sec_ArduBytes[tmpc]);
   }
   
   if (op==6)
   {
     b=pgm_read_byte(&EDC16Info_ArduBytes[tmpc]);
   }
   if (op==7)
   {
     b=pgm_read_byte(&M58BW016xB_Read_Eeprom_ArduBytes[tmpc]);
   }
   if (op==8)
   {
     b=pgm_read_byte(&EDC16Erase_ArduBytes[tmpc]);
   }
   if (op==9)
   {
     b=pgm_read_byte(&Req124k_ArduBytes[tmpc]);
   }
      
   Serial.write(b);
   SendDelay();
   SDbuffer[tmpc]=b;
   totaldelay=totaldelay+65;
  }
  b=iso_checksum(SDbuffer,tmpc);
  Serial.write(b);
  if (setspeed==10400)
  {
    delay(1);
  }
  else
  {
    delayMicroseconds(totaldelay);
  }
  digitalWrite(RXENABLE,LOW);
}  

void WriteString(byte leng)
{
  long totaldelay=25;
  digitalWrite(RXENABLE,HIGH);
  for (byte crap=0; crap<leng; crap++)
  {
    Serial.write(SDbuffer[crap]);
    totaldelay=totaldelay+70;
    SendDelay();
  }
  Serial.write(iso_checksum(SDbuffer,leng));
  if (setspeed==10400)
  {
    delay(1);
  }
  else
  {
    delayMicroseconds(totaldelay);
  }
  digitalWrite(RXENABLE,LOW);
}

void ReadString()
{
  while(Serial.available()<1)
  {}
  byte crap=0;
  while(Serial.available()>0)
  {
    iso_read_byte();
    SDbuffer[crap]=b;
    crap++;
      if (setspeed==10400)
    {
      delay(1);
    }
    else
    {
      delayMicroseconds(30);
    }
  }
}

//***************Data handling and checks**************//


//CRC calculation
byte iso_checksum(byte *data, long len)
{
  byte crc=0;
  for(int i=0; i<len; i++)
    crc=crc+data[i];

  return crc;
}




//Checks if the byte we receive is the one we expect
boolean CheckRec(byte p)
{

   iso_read_byte();
     
      if ( b!= p )
   {
     
         if (iscrc == 1)
      {
        return 0;
      }
      if (b==0)
      {
      return 0;
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
      while (1){};
      }
   }
return 1;
}

  


//********************Menu and buttons handling**************//

byte CheckButtonPressed()
{
  int nopress=1;
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
    lcd.print("                    ");
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
  fail=0;
  setspeed=10400;
  success = 0;
  iscrc=0;
  EDC16ReadDone=0;
  checksum1=0;
  checksum2=0;
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
 
 long StringSearch(char content[], long start, long endd, byte leng)
{
  myFile.seekSet(start);
  char filename[leng];
  byte entryfound=0;
  while (entryfound == 0 && start<endd)//search for the file in the index
    {
      byte counter=0;
      myFile.read(SDbuffer,255);
      while (counter<255 && entryfound == 0)
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
            entryfound=1;
            start=(start-255)+counter;
          }
        }
        counter--;
      }
      counter++;
    }
    start=start+255;
    }
    if (start==endd)
    {
      start=0;
    }
    start=start-leng;
    return start;
}
