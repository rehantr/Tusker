#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <math.h>

// -------------------
// WiFi credentials
// -------------------
const char* ssid = "tusker";
const char* password = "12345679";
const char* ap_ssid = "tuskeraveeeran";
const char* ap_password = "12345678";

WebServer server(80);

// MQ-3 sensor analog pin
const int mq3Pin = 34;

// Dataset row struct
struct Sample {
  const char* label;
  float sensor_value;
};
std::vector<Sample> dataset;

// Pseudo-random LCG (deterministic for repeatable dataset)
uint32_t lcg_state = 123456789UL;
uint32_t lcg_next() {
  lcg_state = (1103515245UL * lcg_state + 12345UL) & 0x7fffffffUL;
  return lcg_state;
}
float lcg_rand01() {
  return (lcg_next() & 0x7fffffffUL) / 2147483648.0;
}

// Create Gaussian-like samples for each brand
void addSamplesForBrand(const char* label, float mean, float stddev, int count) {
  for (int i = 0; i < count; ++i) {
    float u1 = lcg_rand01();
    float u2 = lcg_rand01();
    if (u1 < 1e-6) u1 = 1e-6;
    float z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    float val = mean + z0 * stddev;
    if (val < 0) val = 0;
    dataset.push_back({label, val});
  }
}

// Nearest neighbor by single sensor value
String nearestBrandBySensorValue(float sensorValue) {
  if (dataset.empty()) return "No dataset";
  const Sample* best = nullptr;
  float bestDist = 1e12;
  for (auto &s : dataset) {
    float d = fabs(s.sensor_value - sensorValue);
    if (d < bestDist) {
      bestDist = d;
      best = &s;
    }
  }
  return String(best->label);
}

// HTML page
void handleRoot() {
  const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Alcohol Brand Detector</title>
<style>
body{font-family:Arial;background:#f6f7fb;color:#222;display:flex;justify-content:center;padding:30px;}
.card{width:90%;max-width:720px;background:#fff;padding:20px;border-radius:10px;box-shadow:0 6px 18px rgba(0,0,0,0.08);text-align:center;}
button{background:#6c63ff;color:#fff;padding:10px 18px;border:none;border-radius:6px;cursor:pointer;}
.output{margin-top:18px;padding:10px;border-radius:6px;background:#f0f0f4;}
</style>
</head>
<body>
<div class="card">
  <h1>Alcohol Brand Detection</h1>
  <h2>പാമ്പ് ചാക്കോച്ചൻ മെമ്മോറിയൽ</h2>
  <p>Click to get prediction from the MQ-3 sensor on the ESP32.</p>
  <button onclick="getPrediction()">Get Prediction</button>
  <div class="output" id="prediction">Waiting...</div>
</div>
<script>
function getPrediction(){
  document.getElementById('prediction').textContent = 'Measuring...';
  fetch('/predict').then(r=>r.json()).then(j=>{
    document.getElementById('prediction').textContent =
      'Predicted Brand: ' + j.brand + ' (sensor=' + (j.sensor||'') + ')';
  }).catch(e=>{
    document.getElementById('prediction').textContent = 'Error: ' + e;
    
  });
}
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

// Prediction endpoint
void handlePredict() {
  const int samples = 20;
  const int delayMs = 30;
  float sum = 0;
  for (int i = 0; i < samples; ++i) {
    sum += analogRead(mq3Pin);
    delay(delayMs);
  }
  float sensorValue = sum / samples;
  String brand = nearestBrandBySensorValue(sensorValue);
  Serial.printf("Sensor: %.2f -> %s\n", sensorValue, brand.c_str());

  StaticJsonDocument<200> doc;
  doc["brand"] = brand;
  doc["sensor"] = sensorValue;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Calibrated synthetic dataset values (adjust after measuring real readings)
  addSamplesForBrand("normal", 1250.0f, 10.0f, 0);
  addSamplesForBrand("Dettol Hand Sanitizer", 1300.0f, 10.0f, 1250);
  addSamplesForBrand("Fogg Master Perfume", 1750.0f, 10.0f, 1300);
  addSamplesForBrand("Adidas Dynamic Pulse Perfume", 2200.0f, 10.0f, 1750);

  Serial.printf("Dataset size: %d samples\n", (int)dataset.size());

  // WiFi connect or AP fallback
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to Wi-Fi SSID: %s", ssid);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi failed, starting AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }

  // Server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/predict", HTTP_GET, handlePredict);
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  server.handleClient();
}
