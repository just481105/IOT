#define BLYNK_TEMPLATE_ID "TMPL6JdDH-Rqn"
#define BLYNK_TEMPLATE_NAME "restream hôm qua ............."
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>

// --- CẤU HÌNH CHÂN NGOẠI VI ---
#define SS_PIN    5   // Chân SDA trên RC522
#define RST_PIN   15  // Dùng GPIO 15 cho Reset RFID

#define TRIG_PIN  12  // HC-SR04 Trig
#define ECHO_PIN  14  // HC-SR04 Echo

#define LED_PIN   2   // Chân điều khiển LED đỏ
#define RELAY_PIN 27  // Module Relay quạt
#define LDR_PIN   34  // Cảm biến quang trở
#define DHT_PIN   4   // Cảm biến nhiệt độ DHT22
#define DHTTYPE   DHT22

MFRC522 rfid(SS_PIN, RST_PIN);
DHT dht(DHT_PIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
BlynkTimer timer;

char ssid[] = "Wokwi-GUEST";
char pass[] = "";

const float TEMP_FAN_ON  = 30.0;
const float TEMP_FAN_OFF = 28.0;

// CÀI ĐẶT THỜI GIAN CHỜ TẮT: ĐÚNG 10 GIÂY ĐỂ DEMO
const unsigned long THOI_GIAN_CHO_TAT = 10000UL; // 10 giây (10.000 ms)
unsigned long thoiDiemThayNguoiCuoi = 0;

bool phongDaKichHoat = false;
int siSoHocSinh = 0;

bool adminOverrideLed = false;
bool adminOverrideFan = false;
bool quatDangBat = false;
bool batDen = false;
float temp = 0;
int lightVal = 0;
bool coNguoi = false;

// Blynk V5: Trả quyền tự động
BLYNK_WRITE(V5) {
  if (param.asInt() == 1) {
    adminOverrideLed = false;
    adminOverrideFan = false;
  }
}

// Blynk V3: Bật/Tắt Đèn thủ công
BLYNK_WRITE(V3) {
  adminOverrideLed = true;
  batDen = (param.asInt() == 1);
  digitalWrite(LED_PIN, batDen ? HIGH : LOW);
}

// Blynk V4: Bật/Tắt Quạt thủ công
BLYNK_WRITE(V4) {
  adminOverrideFan = true;
  quatDangBat = (param.asInt() == 1);
  digitalWrite(RELAY_PIN, quatDangBat ? HIGH : LOW);
}

// Hàm kích hoạt mở điện và điểm danh
void kichHoatDiemDanh(String nguonGoi) {
  siSoHocSinh++;
  thoiDiemThayNguoiCuoi = millis(); // Cập nhật mốc thời gian ngay lập tức

  if (!phongDaKichHoat) {
    phongDaKichHoat = true;
    batDen = true;
    digitalWrite(LED_PIN, HIGH);
    Blynk.virtualWrite(V3, 1);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("XIN CHAO HS SO 1");
    lcd.setCursor(0, 1);
    lcd.print("DA MO DIEN PHONG!");
    delay(1500);
    lcd.clear();
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DIEM DANH: OK");
    lcd.setCursor(0, 1);
    lcd.print("SI SO: ");
    lcd.print(siSoHocSinh);
    delay(800);
    lcd.clear();
  }
}

// Quét thẻ RFID
void kiemTraDiemDanhRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uidStr += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  kichHoatDiemDanh("The: " + uidStr);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// Chu kỳ quét cảm biến và điều khiển thiết bị
void xuLyHeThong() {
  // 1. Đo khoảng cách HC-SR04
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float distanceCm = (duration > 0) ? (duration * 0.034 / 2) : 400.0;

  // 2. Đọc cảm biến nhiệt độ & ánh sáng
  float t = dht.readTemperature();
  if (!isnan(t)) temp = t;
  lightVal = analogRead(LDR_PIN);

  // Nhận diện có người (trong cự ly 2cm đến 100cm)
  coNguoi = (distanceCm >= 2.0 && distanceCm < 100.0);

  int giayConLai = 0;

  // 3. Logic điều khiển Đèn & Quạt
  if (phongDaKichHoat) {
    if (coNguoi) {
      // Còn người trong phòng: Liên tục làm mới mốc thời gian
      thoiDiemThayNguoiCuoi = millis();

      if (!adminOverrideLed) {
        batDen = true; // Có người -> Bật đèn
      }

      if (!adminOverrideFan) {
        if (temp >= TEMP_FAN_ON) quatDangBat = true;
        else if (temp < TEMP_FAN_OFF) quatDangBat = false;
      }
    } else {
      // VẮNG NGƯỜI: Tính thời gian đếm ngược 10 giây
      unsigned long thoiGianVang = millis() - thoiDiemThayNguoiCuoi;

      if (thoiGianVang >= THOI_GIAN_CHO_TAT) {
        // Hết 10 giây: Tự động ngắt tất cả và đóng phòng
        if (!adminOverrideLed) batDen = false;
        if (!adminOverrideFan) quatDangBat = false;
        phongDaKichHoat = false;
        siSoHocSinh = 0;
      } else {
        giayConLai = (THOI_GIAN_CHO_TAT - thoiGianVang) / 1000 + 1;
      }
    }
  } else {
    // Chưa mở phòng
    batDen = false;
    quatDangBat = false;
  }

  // Xuất tín hiệu ra mạch vật lý
  digitalWrite(LED_PIN, batDen ? HIGH : LOW);
  digitalWrite(RELAY_PIN, quatDangBat ? HIGH : LOW);

  // Cập nhật lên Blynk
  Blynk.virtualWrite(V3, batDen ? 1 : 0);
  Blynk.virtualWrite(V4, quatDangBat ? 1 : 0);
  Blynk.virtualWrite(V0, temp);
  Blynk.virtualWrite(V1, lightVal);
  Blynk.virtualWrite(V2, coNguoi ? "CO NGUOI" : "PHONG TRONG");
  Blynk.virtualWrite(V6, siSoHocSinh);

  // Hiển thị LCD 16x2
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print((int)temp);
  lcd.print("C L:");
  lcd.print(lightVal);
  lcd.print((adminOverrideFan || adminOverrideLed) ? " [ADM]" : " [AUT]");

  lcd.setCursor(0, 1);
  if (phongDaKichHoat && !coNguoi) {
    // Khi đang đếm ngược: Hiển thị số giây còn lại trực quan
    lcd.print("KO NGUOI OFF:");
    lcd.print(giayConLai);
    lcd.print("s ");
  } else {
    // Trạng thái bình thường
    lcd.print(coNguoi ? "P:CO " : "P:KO ");
    lcd.print("D:");
    lcd.print(batDen ? "1 " : "0 ");
    lcd.print("Q:");
    lcd.print(quatDangBat ? "1" : "0");
    lcd.print(" SS:");
    lcd.print(siSoHocSinh);
    lcd.print(" ");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);

  lcd.init();
  lcd.backlight();

  SPI.begin();
  rfid.PCD_Init();
  dht.begin();

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  Blynk.virtualWrite(V5, 1);

  // Quét chu kỳ 500ms
  timer.setInterval(500L, xuLyHeThong);
}

void loop() {
  Blynk.run();
  timer.run();
  kiemTraDiemDanhRFID();
}
