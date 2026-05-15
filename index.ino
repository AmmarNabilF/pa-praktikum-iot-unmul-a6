#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <UniversalTelegramBot.h>

// --- Konfigurasi WiFi ---
const char* ssid = "cruxx";
const char* password = "siapatawu";

// --- Konfigurasi MQTT & Telegram ---
const char* mqtt_server = "broker.emqx.io"; 
const char* botToken = "8611433066:AAG6ifHeXbwuFbDWYBD6_-oZgfCxmm5Llo8"; 
const char* chatId = "-1003929164178";

// Pin Hardware
#define PIR_PIN 19
#define TRIG_PIN 5
#define ECHO_PIN 18
#define BUZZER_PIN 22
#define LED_PIN 23

#define LED_ON_DURATION 6000  // 6 detik

WiFiClient espClientMQTT;
WiFiClientSecure espClientTelegram;

PubSubClient client(espClientMQTT);
UniversalTelegramBot bot(botToken, espClientTelegram);

bool modeManual = false;
bool statusLampu = false;
bool notifPintuTerkirim = false;
unsigned long lastCheck = 0;
unsigned long lastBotRun = 0;
int botReqDelay = 800;

unsigned long lastMotionTime = 0;   // Kapan terakhir kali gerakan terdeteksi
bool motionActive = false;          // Apakah sedang dalam periode "nyala pasca gerakan"

void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("\n--- Sistem Smart Room Dimulai ---");

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  setup_wifi();
  espClientTelegram.setInsecure(); 
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void setup_wifi() {
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void handleMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id; 
    String sender_name = bot.messages[i].from_name;
    String text = bot.messages[i].text;

    if (chat_id != chatId) continue;

    if (text == "/lamp_on") {
      modeManual = true;
      statusLampu = true;
      bot.sendMessage(chat_id, "Lampu dinyalakan oleh " + sender_name, "");
    } 
    else if (text == "/lamp_off") {
      modeManual = true;
      statusLampu = false;

      // reset timer kalo dimatikan manual
      motionActive = false;
      bot.sendMessage(chat_id, "Lampu dimatikan oleh " + sender_name, "");
    } 
    else if (text == "/change_mode") {
      modeManual = !modeManual;
      String modeText = modeManual ? "Manual" : "Auto";
      bot.sendMessage(chat_id, "Sistem sekarang berada di mode: " + modeText, "");
    }
    else if (text == "/status") {
      String lampStatus = statusLampu ? "ON" : "OFF";
      String modeStatus = modeManual ? "Manual" : "Auto";
      unsigned long sisaWaktu = 0;
      if (!modeManual && motionActive) {
        unsigned long elapsed = millis() - lastMotionTime;
        sisaWaktu = elapsed < LED_ON_DURATION ? (LED_ON_DURATION - elapsed) / 1000 : 0;
      }
      String pesan = "Status Smart Room:\n";
      pesan += "• Lampu: " + lampStatus + "\n";
      pesan += "• Mode: " + modeStatus + "\n";
      if (!modeManual && motionActive && sisaWaktu > 0) {
        pesan += "• LED mati dalam: " + String(sisaWaktu) + " detik";
      }
      bot.sendMessage(chat_id, pesan, "");
    }
    else if (text == "/start") {
      String pesan = "Selamat Datang di Smart Room Bot! 🏠\n\n";
      pesan += "Perintah yang tersedia:\n";
      pesan += "/lamp_on - Nyalakan lampu\n";
      pesan += "/lamp_off - Matikan lampu\n";
      pesan += "/change_mode - Ganti mode Auto/Manual\n";
      pesan += "/status - Cek status sistem\n"; 
      pesan += "/start - Tampilkan pesan ini";
      bot.sendMessage(chat_id, pesan, "");
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Pesan MQTT masuk [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println(msg);

  char command = msg[0];
  if (command == '1') { 
    modeManual = true; 
    statusLampu = true; 
  }
  else if (command == '0') { 
    modeManual = true; 
    statusLampu = false; 
    // Reset timer kalo dimatikan via MQTT
    motionActive = false;
  }
  else if (command == 'A') { 
    modeManual = false; 
  } 

  client.publish("smartroom/lampu/mode", modeManual ? "Manual" : "Auto");
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // Pengecekan Pesan Telegram
  if (millis() - lastBotRun > botReqDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastBotRun = millis();
  }

  if (!modeManual) {
    if (motionActive) {
      unsigned long elapsed = millis() - lastMotionTime;
      if (elapsed >= LED_ON_DURATION) {
        // Timer habis, lampu mati
        statusLampu = false;
        motionActive = false;
        digitalWrite(LED_PIN, LOW);
        Serial.println("[AUTO] Timer habis - Lampu MATI");
        client.publish("smartroom/lampu/status", "OFF");
      }
    }
  }

  if (millis() - lastCheck > 5000) {
    Serial.println("--- Debug Update ---");
    
    // ULTRASONIK
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
    int distance = duration * 0.034 / 2;

    Serial.print("Jarak: ");
    if (distance <= 0) Serial.println("Error (Sensor tidak terbaca)");
    else { Serial.print(distance); Serial.println(" cm"); }

    if (distance > 10 && distance < 400) { 
      digitalWrite(BUZZER_PIN, HIGH);
      if (!notifPintuTerkirim) {
        if(bot.sendMessage(chatId, "Peringatan: Pintu Terbuka!", "")) 
          Serial.println("Telegram OK");
        else 
          Serial.println("Telegram Gagal");
        notifPintuTerkirim = true; 
      }
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      notifPintuTerkirim = false; 
    }

    // PIR
    int pirValue = digitalRead(PIR_PIN);
    Serial.print("Status PIR: ");
    Serial.println(pirValue == HIGH ? "GERAKAN TERDETEKSI!" : "Tenang");

    if (!modeManual) {
      if (pirValue == HIGH) {
        // Catat waktu gerakan, nyalakan LED
        lastMotionTime = millis();
        motionActive = true;
        statusLampu = true;
        digitalWrite(LED_PIN, HIGH);
        Serial.println("[AUTO] Gerakan terdeteksi - Lampu NYALA, timer 6 detik dimulai");
      } else {
        Serial.print("[AUTO] Tidak ada gerakan. ");
        if (motionActive) {
          unsigned long sisa = LED_ON_DURATION - (millis() - lastMotionTime);
          Serial.print("Lampu mati dalam ~");
          Serial.print(sisa / 1000);
          Serial.println(" detik...");
        } else {
          Serial.println("Lampu tetap MATI.");
        }
      }
    }
    
    // Update status fisik LED (hanya untuk mode manual, 
    // mode auto sudah dihandle di atas)
    if (modeManual) {
      digitalWrite(LED_PIN, statusLampu ? HIGH : LOW);
    }

    Serial.print("Status Lampu: ");
    Serial.println(statusLampu ? "ON" : "OFF");

    // MQTT Publish
    client.publish("smartroom/lampu/status", statusLampu ? "ON" : "OFF");
    char jarakStr[10];
    dtostrf(distance, 1, 0, jarakStr);
    client.publish("smartroom/pintu/jarak", jarakStr);
    client.publish("smartroom/lampu/mode", modeManual ? "Manual" : "Auto");
    
    Serial.println("--------------------\n");
    lastCheck = millis();
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke Broker MQTT...");
    String clientId = "SmartRoom_Device_";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {   
      Serial.println("Terhubung ke EMQX!");
      client.subscribe("smartroom/lampu/kendali");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(". Coba lagi 5 detik...");
      delay(5000);
    }
  }
}
