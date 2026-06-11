#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Servo.h>
#include <nRF24L01.h>
#include <RF24.h>

#define RFID_SS_PIN 8
#define RFID_RST_PIN 5
#define SERVO_PIN 3
#define NRF_CE_PIN 6
#define NRF_CSN_PIN 7

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
MFRC522::MIFARE_Key key;
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myservo;
RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'7','8','9','/'},
  {'4','5','6','X'},
  {'1','2','3','-'},
  {'C','0','=','+'}
};
byte rowPins[ROWS] = {2, 4, 10, 9};
byte colPins[COLS] = {17, 16, 15, 14};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

struct DataPacket {
  uint32_t cardID;
  int liters;
  long remainingBalance;
};

void setup() {
  SPI.begin();
  mfrc522.PCD_Init();
  lcd.init();
  lcd.backlight();
  myservo.attach(SERVO_PIN);
  myservo.write(0);
  radio.begin();
  radio.openWritingPipe(0xF0F0F0F0E1LL);
  radio.setPALevel(RF24_PA_LOW);
  radio.stopListening();
  
  for (byte i = 0; i < 6; i++) key.keyA[i] = 0xFF;
  showIdleMessage();
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;
  handleTransaction();
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  showIdleMessage();
}

void showIdleMessage() {
  lcd.clear();
  lcd.print("Prisentez Carte");
  lcd.setCursor(0, 1);
  lcd.print("100FC = 20L");
}

void handleTransaction() {
  long balance = readBalance();
  if (balance < 0) {
    displayError("Erreur Carte");
    return;
  }

  lcd.clear();
  lcd.print("Solde: ");
  lcd.print(balance);
  lcd.print(" FC");
  delay(1500);

  int requestedLiters = getRequestedLiters();
  if (requestedLiters <= 0) return;

  long cost = requestedLiters * 5; 

  if (balance >= cost) {
    long newBalance = balance - cost;
    if (updateBalance(newBalance)) {
      lcd.clear();
      lcd.print("Debit: ");
      lcd.print(cost);
      lcd.print(" FC");
      dispenseWater(requestedLiters);
      sendTelemetry(requestedLiters, newBalance);
    } else {
      displayError("Echec Ecriture");
    }
  } else {
    displayError("Solde Insuff.");
  }
}

int getRequestedLiters() {
  lcd.clear();
  lcd.print("Litres: ");
  String input = "";
  while (true) {
    char keyInput = keypad.getKey();
    if (keyInput >= '0' && keyInput <= '9') {
      input += keyInput;
      lcd.setCursor(8, 0);
      lcd.print(input);
    } else if (keyInput == '=') {
      return input.toInt();
    } else if (keyInput == 'C') {
      return 0;
    }
  }
}

void dispenseWater(int liters) {
  lcd.setCursor(0, 1);
  lcd.print("Distribution...");
  myservo.write(90);
  delay(liters * 1000); 
  myservo.write(0);
  lcd.clear();
  lcd.print("Termine !");
  delay(2000);
}

long readBalance() {
  byte block = 4;
  byte buffer[18];
  byte size = sizeof(buffer);
  
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) return -1;

  status = mfrc522.MIFARE_Read(block, buffer, &size);
  if (status != MFRC522::STATUS_OK) return -1;

  return ((long)buffer[0] << 24) | ((long)buffer[1] << 16) | ((long)buffer[2] << 8) | (long)buffer[3];
}

bool updateBalance(long newBalance) {
  byte block = 4;
  byte dataBlock[16] = {0};
  dataBlock[0] = (newBalance >> 24) & 0xFF;
  dataBlock[1] = (newBalance >> 16) & 0xFF;
  dataBlock[2] = (newBalance >> 8) & 0xFF;
  dataBlock[3] = newBalance & 0xFF;

  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) return false;

  status = mfrc522.MIFARE_Write(block, dataBlock, 16);
  return (status == MFRC522::STATUS_OK);
}

void sendTelemetry(int liters, long balance) {
  DataPacket packet;
  packet.cardID = 0; 
  for (byte i = 0; i < 4; i++) packet.cardID = (packet.cardID << 8) | mfrc522.uid.uidByte[i];
  packet.liters = liters;
  packet.remainingBalance = balance;
  radio.write(&packet, sizeof(packet));
}

void displayError(String msg) {
  lcd.clear();
  lcd.print(msg);
  delay(2000);
}