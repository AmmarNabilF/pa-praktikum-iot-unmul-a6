#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <UniversalTelegramBot.h>

// --- Konfigurasi WiFi Lama ---
const char* ssid = "ADD SSID";
const char* password = "ADD PW";

// --- Konfigurasi MQTT & Telegram ---
const char* mqtt_server = "broker.emqx.io"; // Diubah ke EMQX
const char* botToken = "8611433066:AAG6ifHeXbwuFbDWYBD6_-oZgfCxmm5Llo8"; 
const char* chatId = "-1003929164178";

// Pin Hardware
#define PIR_PIN 19
#define TRIG_PIN 5
#define ECHO_PIN 18
#define BUZZER_PIN 22
#define LED_PIN 23

WiFiClientSecure espClient;
PubSubClient client(espClient);
UniversalTelegramBot bot(botToken, espClient);

bool modeManual = false;
bool statusLampu = false;
bool notifTerkirim = false; 
unsigned long lastCheck = 0;

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
  
  // Karena Telegram Bot wajib pakai jalur aman (TLS/SSL), MQTT juga pakai port 8883
  espClient.setInsecure(); 
  client.setServer(mqtt_server, 8883); // Port 8883 tetap digunakan karena espClient adalah WiFiClientSecure
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

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Pesan MQTT masuk [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println(msg);

  char command = msg[0];
  if (command == '1') { modeManual = true; statusLampu = true; }
  else if (command == '0') { modeManual = true; statusLampu = false; }
  else if (command == 'A') { modeManual = false; } 

  client.publish("smartroom/lampu/mode", modeManual ? "Manual" : "Auto");
  Serial.print("Mode sekarang: ");
  Serial.println(modeManual ? "MANUAL" : "AUTO");
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  if (millis() - lastCheck > 1000) { // Debug setiap 1 detik agar tidak terlalu cepat
    Serial.println("--- Debug Update ---");
    
    // --- LOGIKA A: ULTRASONIK ---
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout 30ms jika sensor tidak merespon
    int distance = duration * 0.034 / 2;

    Serial.print("Jarak: ");
    if (distance <= 0) Serial.println("Error (Sensor tidak terbaca)");
    else { Serial.print(distance); Serial.println(" cm"); }

    if (distance > 10 && distance < 400) { 
      digitalWrite(BUZZER_PIN, HIGH);
      if (!notifTerkirim) {
        Serial.println("Memicu Bot Telegram...");
        if(bot.sendMessage(chatId, "⚠️ Peringatan: Pintu Terbuka!", "")) Serial.println("Telegram OK");
        else Serial.println("Telegram Gagal");
        notifTerkirim = true; 
      }
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      notifTerkirim = false; 
    }

    // --- LOGIKA B: PIR ---
    int pirValue = digitalRead(PIR_PIN);
    Serial.print("Status PIR: ");
    Serial.println(pirValue == HIGH ? "GERAKAN!" : "Tenang");

    if (!modeManual) {
      statusLampu = (pirValue == HIGH);
    }
    
    digitalWrite(LED_PIN, statusLampu ? HIGH : LOW);
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
    
    // Membuat Client ID acak agar unik di public broker
    String clientId = "SmartRoom_Device_";
    clientId += String(random(0xffff), HEX);
    
    // Koneksi ke EMQX public tidak memerlukan username dan password
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
