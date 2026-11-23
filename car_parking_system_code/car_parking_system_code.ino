#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

MFRC522 rfid(10, 9); // SS = D10, RST = D9
Servo gateServo;
LiquidCrystal_I2C lcd(0x27, 16, 2); // Đổi 0x27 thành 0x3F nếu màn sáng nhưng không hiện chữ

#define IR_ENTRY_PIN 5   // Cảm biến IR phía NGOÀI cổng (trước barrier)
#define IR_EXIT_PIN 4    // Cảm biến IR phía TRONG cổng (sau barrier)
#define SERVO_PIN 6      // Động cơ Servo

const int slotPins[4] = {A0, A1, A2, A3};
int lastSlotState[4] = {HIGH, HIGH, HIGH, HIGH};

int totalSlots = 4;
int availableSlots = 4;

String validUID = "84 BF BA 91"; // Thẻ hợp lệ mở barrier

// Hàm chờ xe đi qua một cảm biến (chờ chạm LOW rồi rời khỏi HIGH, giới hạn 10s tránh treo)
void waitCarPassSensor(int sensorPin) {
  unsigned long startTime = millis();
  // Bước 1: Chờ đầu xe chạm vào cảm biến (LOW)
  while (digitalRead(sensorPin) == HIGH) {
    if (millis() - startTime > 10000) return; // Quá 10s không có xe qua thì tự đóng
    delay(20);
  }
  // Bước 2: Chờ đuôi xe đi thoát hẳn qua khỏi cảm biến (HIGH)
  while (digitalRead(sensorPin) == LOW) {
    if (millis() - startTime > 15000) return;
    delay(20);
  }
  delay(400); // Đợi thêm 0.4s cho đuôi xe qua hẳn thanh chắn rồi mới đóng
}

void setup() {
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();

  rfid.PCD_DumpVersionToSerial();
  Serial.println("He thong san sang!");

  pinMode(IR_ENTRY_PIN, INPUT);
  pinMode(IR_EXIT_PIN, INPUT);

  for (int i = 0; i < 4; i++) {
    pinMode(slotPins[i], INPUT);
    lastSlotState[i] = digitalRead(slotPins[i]);
  }

  gateServo.attach(SERVO_PIN);
  gateServo.write(90); // Đóng barrier ban đầu (90 độ)

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("  SMART PARKING ");
  lcd.setCursor(0, 1);
  lcd.print("    WELCOME!    ");
  delay(2000);

  updateLCD();
}

void loop() {
  // 1. KIỂM TRA TRẠNG THÁI 4 Ô ĐỖ XE LIÊN TỤC (A0 - A3)
  bool stateChanged = false;
  int emptyCount = 0;

  for (int i = 0; i < 4; i++) {
    int currentState = digitalRead(slotPins[i]);
    if (currentState == HIGH) { // HIGH = Ô trống
      emptyCount++;
    }
    if (currentState != lastSlotState[i]) {
      lastSlotState[i] = currentState;
      stateChanged = true;
    }
  }
  availableSlots = emptyCount;

  if (stateChanged) {
    updateLCD();
  }

  // 2. LUỒNG XE ĐI RA (Chạm IR trong D4 -> Mở barrier -> Chờ qua IR ngoài D5 -> Đóng barrier)
  if (digitalRead(IR_EXIT_PIN) == LOW) {
    Serial.println("[XE RA] Phat hien xe tai D4 -> Mo barrier & GOODBYE!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("    GOODBYE!    ");
    lcd.setCursor(0, 1);
    lcd.print("  SEE YOU AGAIN ");

    gateServo.write(180);               // Mở barrier (180 độ)
    waitCarPassSensor(IR_ENTRY_PIN);    // Chờ xe đi qua hẳn cảm biến ngoài (D5)
    gateServo.write(90);                // Đóng barrier (90 độ)

    Serial.println("[XE RA] Xe da qua D5 -> Dong barrier!");
    updateLCD();
    delay(300);
  }

  // 3. ĐỌC THẺ RFID CHO LUỒNG XE VÀO
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String scannedUID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      scannedUID += '0';
    }
    scannedUID += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) {
      scannedUID += ' ';
    }
  }
  scannedUID.toUpperCase();

  Serial.print("Ma UID the: ");
  Serial.println(scannedUID);

  // 4. LUỒNG XE ĐI VÀO (Có xe ở D5 -> Quẹt thẻ đúng -> Mở barrier -> Chờ qua D4 -> Đóng barrier)
  if (digitalRead(IR_ENTRY_PIN) == HIGH) {
    Serial.println("-> Chua co xe truoc D5! ACCESS DENIED");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" NO CAR AT GATE ");
    lcd.setCursor(0, 1);
    lcd.print("  ACCESS DENIED ");
    delay(1500);
    updateLCD();
  }
  else if (availableSlots <= 0) {
    Serial.println("-> Bai da day! ACCESS DENIED");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  PARKING FULL! ");
    lcd.setCursor(0, 1);
    lcd.print("  ACCESS DENIED ");
    delay(1500);
    updateLCD();
  }
  else if (scannedUID == validUID) {
    Serial.println("[XE VAO] The hop le -> Mo barrier, cho xe qua D4...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" ACCESS GRANTED ");
    lcd.setCursor(0, 1);
    lcd.print("  OPENING GATE  ");

    gateServo.write(180);               // Mở barrier (180 độ)
    waitCarPassSensor(IR_EXIT_PIN);     // Chờ xe đi qua hẳn cảm biến trong (D4)
    gateServo.write(90);                // Đóng barrier (90 độ)

    Serial.println("[XE VAO] Xe da qua D4 -> Dong barrier!");
    updateLCD();
  }
  else {
    Serial.println("-> Sai the! ACCESS DENIED");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("  INVALID CARD  ");
    lcd.setCursor(0, 1);
    lcd.print("  ACCESS DENIED ");
    delay(1500);
    updateLCD();
  }

  rfid.PICC_HaltA();
}

// Màn hình mặc định: S1..S4 (EMPTY / FULL)
void updateLCD() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("S1:");
  lcd.print(digitalRead(slotPins[0]) == LOW ? "FULL " : "EMPTY");

  lcd.setCursor(9, 0);
  lcd.print("S2:");
  lcd.print(digitalRead(slotPins[1]) == LOW ? "FULL " : "EMPTY");

  lcd.setCursor(0, 1);
  lcd.print("S3:");
  lcd.print(digitalRead(slotPins[2]) == LOW ? "FULL " : "EMPTY");

  lcd.setCursor(9, 1);
  lcd.print("S4:");
  lcd.print(digitalRead(slotPins[3]) == LOW ? "FULL " : "EMPTY");
}