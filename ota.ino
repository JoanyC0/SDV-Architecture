#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>

// ================= CONFIGURATION =================
const char* ssid     = "Taona_zina_2.4G";
const char* password = "Chezrasta_2026";

#define UART_TX     17
#define UART_RX     16
#define UART_BAUD   115200
#define STM32_RESET_PIN 4
#define BLOCK_SIZE   256

// Commandes UART
#define CMD_START_UPDATE    0x55
#define CMD_DATA_PACKET     0xAA
#define CMD_UPDATE_COMPLETE 0xFF
#define RSP_OK              0x10
#define RSP_ERROR           0x11
#define RSP_ERASE_DONE      0x12

// Timeouts et retries
#define ACK_TIMEOUT         3000
#define PACKET_TIMEOUT      3000
#define MAX_RETRIES         5
#define MAX_ATTEMPTS        10

// ================= GLOBALES =================
WebServer server(80);
File uploadFile;
unsigned long startTime;
bool updateSuccessful = false;
bool otaInProgress = false;
int packetCount = 0;

// ================= FONCTIONS UART =================
bool waitForAck(int timeout_ms, const char* context, uint8_t expected = RSP_OK) {
    unsigned long start = millis();
    while (millis() - start < timeout_ms) {
        if (Serial2.available()) {
            uint8_t response = Serial2.read();
            if (response == expected || response == RSP_OK) {
                return true;
            } else if (response == RSP_ERROR) {
                Serial.printf("[STM32] ERROR response (%s)\n", context);
                return false;
            }
        }
        delay(1);
    }
    Serial.printf("[STM32] ACK timeout (%s)\n", context);
    return false;
}

void clearUartBuffer() {
    while (Serial2.available()) Serial2.read();
}

bool sendPacket(uint8_t* data, int len, int packetNum) {
    clearUartBuffer();
    delay(10);
    int written = Serial2.write(data, len);
    Serial2.flush();
    if (written != len) {
        Serial.printf("[ERROR] Failed to send complete packet (%d/%d)\n", written, len);
        return false;
    }
    char context[32];
    sprintf(context, "Packet %d", packetNum);
    return waitForAck(PACKET_TIMEOUT, context);
}

// ================= FONCTION OTA (depuis un buffer en mémoire) =================
bool performOTAFromBuffer(uint8_t* buffer, size_t fileSize) {
    startTime = millis();
    packetCount = 0;
    bool success = false;
    int totalSize = fileSize;

    Serial.printf("[OTA] Firmware size: %d bytes (%.2f KB)\n", totalSize, totalSize/1024.0);
    otaInProgress = true;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS && !success; attempt++) {
        if (attempt > 1) {
            Serial.printf("\n[ATTEMPT] Retry %d/%d\n", attempt, MAX_ATTEMPTS);
            delay(2000);
        }

        // 1. Envoi START
        Serial.print("[STM32] Sending START... ");
        clearUartBuffer();
        Serial2.write(CMD_START_UPDATE);
        if (!waitForAck(ACK_TIMEOUT, "START")) {
            Serial.println("FAILED");
            continue;
        }
        Serial.println("OK");

        // 2. Attendre ERASE_DONE
        Serial.print("[STM32] Waiting for erase done... ");
        if (!waitForAck(10000, "ERASE_DONE", RSP_ERASE_DONE)) {
            Serial.println("FAILED");
            continue;
        }
        Serial.println("OK");

        Serial.println("\n[PROGRESS] Starting transfer...");
        int bytesRead = 0;
        int lastPercent = 0;
        packetCount = 0;

        // 3. Envoi des paquets
        while (bytesRead < totalSize) {
            int bytesToSend = (totalSize - bytesRead > BLOCK_SIZE) ? BLOCK_SIZE : (totalSize - bytesRead);
            uint8_t packet[BLOCK_SIZE + 4];
            packet[0] = CMD_DATA_PACKET;
            packet[1] = (bytesToSend >> 8) & 0xFF;
            packet[2] = bytesToSend & 0xFF;
            packet[3] = 0;
            memcpy(&packet[4], buffer + bytesRead, bytesToSend);

            bool sent = false;
            for (int retry = 0; retry < MAX_RETRIES && !sent; retry++) {
                if (retry > 0) {
                    Serial.printf("[RETRY] Packet %d (try %d)\n", packetCount + 1, retry + 1);
                    delay(100);
                }
                sent = sendPacket(packet, bytesToSend + 4, packetCount + 1);
            }
            if (!sent) {
                Serial.printf("[ERROR] Failed to send packet %d\n", packetCount + 1);
                break;
            }

            bytesRead += bytesToSend;
            packetCount++;

            int percent = (bytesRead * 100) / totalSize;
            if (percent >= lastPercent + 5 || percent == 100) {
                lastPercent = percent;
                float elapsed = (millis() - startTime) / 1000.0;
                float speed = (bytesRead / 1024.0) / elapsed;
                Serial.printf("[%d%%] %d/%d KB - %.1f KB/s\n", percent, bytesRead/1024, totalSize/1024, speed);
            }
        }

        if (bytesRead < totalSize) {
            Serial.println("[ERROR] Transfer incomplete, retrying...");
            continue;
        }

        // 4. Envoi COMPLETE
        Serial.print("\n[STM32] Sending COMPLETE... ");
        uint8_t complete[4] = {CMD_UPDATE_COMPLETE, 0, 0, 0};
        Serial2.write(complete, 4);
        if (waitForAck(ACK_TIMEOUT, "COMPLETE")) {
            Serial.println("OK");
            success = true;
        } else {
            Serial.println("FAILED");
        }
    }

    float totalTime = (millis() - startTime) / 1000.0;
    float avgSpeed = (totalSize / 1024.0) / totalTime;

    Serial.println("\n==========================================");
    Serial.println("         UPDATE SUMMARY");
    Serial.println("==========================================");
    Serial.printf("Status: %s\n", success ? "✓ SUCCESS" : "✗ FAILED");
    Serial.printf("Time: %.1f seconds\n", totalTime);
    Serial.printf("Speed: %.1f KB/s\n", avgSpeed);
    Serial.printf("Packets: %d\n", packetCount);
    Serial.println("==========================================");

    otaInProgress = false;
    return success;
}

// ================= GESTIONNAIRES WEB =================
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>STM32 OTA Manager</title>

<style>

body{
font-family: Arial;
background:#0f172a;
color:white;
text-align:center;
padding:40px;
}

.card{
background:#1e293b;
padding:30px;
border-radius:10px;
width:500px;
margin:auto;
box-shadow:0 0 20px rgba(0,0,0,0.5);
}

button{
background:#3b82f6;
border:none;
padding:12px 20px;
border-radius:5px;
color:white;
font-size:16px;
cursor:pointer;
}

button:hover{
background:#2563eb;
}

.progress{
width:100%;
background:#334155;
border-radius:5px;
margin-top:20px;
}

.bar{
height:20px;
width:0%;
background:#22c55e;
border-radius:5px;
transition:0.3s;
}

.log{
background:black;
color:#0f0;
height:150px;
overflow:auto;
padding:10px;
margin-top:20px;
font-size:12px;
text-align:left;
}

</style>
</head>

<body>

<div class="card">

<h2>STM32 OTA Firmware Manager</h2>

<input type="file" id="file">

<br><br>

<button onclick="upload()">Upload Firmware</button>

<div class="progress">
<div class="bar" id="bar"></div>
</div>

<p id="status">Idle</p>

<div class="log" id="log"></div>

</div>

<script>

function log(msg){
let l=document.getElementById("log");
l.innerHTML += msg+"<br>";
l.scrollTop=l.scrollHeight;
}

function upload(){

let file=document.getElementById("file").files[0];

if(!file){
alert("Select firmware first");
return;
}

let xhr=new XMLHttpRequest();
let form=new FormData();

form.append("firmware",file);

xhr.upload.onprogress=function(e){

if(e.lengthComputable){

let percent=(e.loaded/e.total)*100;

document.getElementById("bar").style.width=percent+"%";
document.getElementById("status").innerText="Uploading "+percent.toFixed(1)+"%";

}

};

xhr.onload=function(){

log("Upload finished");
document.getElementById("status").innerText="OTA started. Check serial logs.";

};

xhr.open("POST","/upload",true);
xhr.send(form);

log("Uploading firmware...");

}

setInterval(()=>{

fetch("/status")
.then(r=>r.json())
.then(d=>{

if(d.status=="success"){

document.getElementById("status").innerText="Update Success";
document.getElementById("bar").style.width="100%";

}
if(d.status=="failed"){

document.getElementById("status").innerText="Update Failed";
document.getElementById("bar").style.width="0%";

}
if(d.status=="in_progress"){

document.getElementById("status").innerText="OTA in progress...";

}

})

},2000);

</script>

</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
}

void handleUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        if (!SPIFFS.begin(true)) {
            server.send(500, "text/plain", "SPIFFS error");
            return;
        }
        uploadFile = SPIFFS.open("/firmware.bin", FILE_WRITE);
        if (!uploadFile) {
            server.send(500, "text/plain", "Failed to create file");
            return;
        }
        Serial.println("[UPLOAD] Started");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!uploadFile) {
            server.send(500, "text/plain", "No file");
            return;
        }
        uploadFile.close();
        Serial.printf("[UPLOAD] Finished, size: %u\n", upload.totalSize);

        File file = SPIFFS.open("/firmware.bin", FILE_READ);
        if (!file) {
            server.send(500, "text/plain", "Failed to open file");
            return;
        }
        size_t fileSize = file.size();
        uint8_t *buffer = (uint8_t*)malloc(fileSize);
        if (!buffer) {
            file.close();
            server.send(500, "text/plain", "Memory error");
            return;
        }
        file.read(buffer, fileSize);
        file.close();

        // Répondre immédiatement pour éviter timeout
        server.send(200, "text/plain", "Upload successful, starting OTA...");

        // Lancer l'OTA (ne pas bloquer la réponse, mais on a déjà envoyé)
        Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX, UART_TX);
        pinMode(STM32_RESET_PIN, OUTPUT);
        digitalWrite(STM32_RESET_PIN, HIGH);
        delay(1000);

        bool result = performOTAFromBuffer(buffer, fileSize);
        free(buffer);

        if (result) {
            Serial.println("\n[STM32] Resetting...");
            delay(1000);
            digitalWrite(STM32_RESET_PIN, LOW);
            delay(100);
            digitalWrite(STM32_RESET_PIN, HIGH);
            Serial.println("[STM32] Reset complete");
            updateSuccessful = true;
        } else {
            Serial.println("[OTA] Failed after retries");
            updateSuccessful = false;
        }
    }
}

void handleStatus() {
    String json;
    if (otaInProgress) {
        json = "{\"status\":\"in_progress\"}";
    } else if (updateSuccessful) {
        json = "{\"status\":\"success\"}";
    } else {
        json = "{\"status\":\"idle\"}";
    }
    server.send(200, "application/json", json);
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n==========================================");
    Serial.println("   ESP32 STM32 OTA UPDATER with Web UI");
    Serial.println("==========================================");

    WiFi.begin(ssid, password);
    Serial.print("[WiFi] Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\n[WiFi] Connected!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);
    server.on("/upload", HTTP_POST, []() {}, handleUpload);
    server.on("/status", handleStatus);

    server.begin();
    Serial.println("[HTTP] Server started");
}

void loop() {
    server.handleClient();
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 30000) {
        lastStatus = millis();
        if (updateSuccessful) {
            Serial.printf("[OK] Running - %lus\n", millis()/1000);
        }
    }
    delay(10);
}