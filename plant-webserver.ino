/*********
  Based on the following source by Rui Santos: http://randomnerdtutorials.com
*********/

// Load Wi-Fi library
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "DHT.h"
#include <ArduinoJson.h>
#include <WiFiClientSecureBearSSL.h>
#include "arduino_secrets.h"

// Digital pins
const int DHTPIN = D1;
const int pirSensor = D3;
const int CLK = D8;
const int DIO = D7;

// Analog pins
int moisturepin = A0;

const int pumpPin = D5;

const int DHTTYPE = DHT22; // DHT 22  (AM2302)

//std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
WiFiClient client;
HTTPClient https;
WiFiServer server(80);
DHT dht(DHTPIN, DHTTYPE);

// Variable to store the HTTP request
String HTTPheader;

String pumpState = "off";

unsigned long currentTime = millis();
unsigned long previousUpdateTime = 0;
unsigned long previousPumpHighTime = 0;
unsigned long previousTime = 0;
const long timeoutTime = 2000; // ms
const long updateTime = 60 * 1000; // ms
const long maxPumpTime = 5 * 1000; // ms

float temperature;
float humidity;
float heatindex;
float moisture_humidity;
bool movement_detected;

String movement_formatted;
String moisture_humidity_formatted;
String humidity_formatted;
String temperature_formatted;
String heatindex_formatted;

String fingerprint;

void setup()
{
    Serial.begin(115200);

    dht.begin();

    pinMode(pumpPin, OUTPUT);
    pinMode(moisturepin, INPUT);

    digitalWrite(pumpPin, HIGH);

    // Connect to Wi-Fi network with SSID and password
    Serial.print("Connecting to ");
    Serial.println(SECRET_SSID);
    WiFi.begin(SECRET_SSID, SECRET_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    // Print local IP address and start web server
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    server.begin();
}

int wet = 368;
int dry = 762;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta http-equiv="Content-Security-Policy" content="style-src 'self' 'unsafe-inline' https://use.fontawesome.com; script-src 'self';">
  <meta charset="utf-8">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <title>Plant monitor</title>
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 3.0rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.2rem; }
    .dht-labels{
      font-size: 1.5rem;
      vertical-align:middle;
      padding-bottom: 15px;
    }
    .button {
        background-color: #195B6A;
        border: none;
        color: white;
        padding: 16px 40px;
        text-decoration: none;
        font-size: 30px;
        margin: 2px;
        cursor: pointer;
    }
  </style>
  <script type="text/javascript" src="/index.js"></script>
</head>
<body>
  <h2>Plant Monitor</h2>
  <p>
    <i class="fas fa-thermometer-half" style="color:#960096;"></i>
    <span class="dht-labels">Temperature</span>
    <span id="temperature">%TEMPERATURE%</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i>
    <span class="dht-labels">Humidity</span>
    <span id="humidity">%HUMIDITY%</span>
    <sup class="units">&percnt;</sup>
  </p>
  <p>
    <i class="fas fa-seedling" style="color:#0cb504;"></i>
    <span class="dht-labels">Moisture</span>
    <span id="moisture">%MOISTURE%</span>
    <sup class="units">&percnt;</sup>
  </p>
  <p>
    <i class="fas fa-fighter-jet" style="color:#c75648;"></i>
    <span class="dht-labels">Movement</span>
    <span id="movement">%MOVEMENT%</span>
  </p>
  %LED_BUTTON%
</body>
</html>)rawliteral";

const char index_js[] PROGMEM = R"rawliteral(
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("temperature").innerHTML = this.responseText.split("\n")[0];
      document.getElementById("humidity").innerHTML = this.responseText.split("\n")[1];
      document.getElementById("moisture").innerHTML = this.responseText.split("\n")[2];
      document.getElementById("movement").innerHTML = this.responseText.split("\n")[3];
    }
  };
  xhttp.open("GET", "/data", true);
  xhttp.send();
}, 60000);)rawliteral";

void send_data(const String& entity_id, const String& device_class, float value, const String& unit)
{
    // Retrieve SSL fingerprint if not stored already.
    // Requesting the fingerprint from the website belonging to the SSL certificate's fingerprint
    // (via HTTP) is typically not so secure. However, in this case we only use it to send simple
    // sensor data, so there is little risk involved.
    if (fingerprint == "" && SECRET_FINGERPRINT_URL != "") {
        Serial.println("Obtaining fingerprint...");
        WiFiClient http_client;
        https.begin(http_client, String(SECRET_FINGERPRINT_URL));
        https.GET();
        fingerprint = https.getString();
        https.end();

        uint fingerprint_length = fingerprint.length() + 1;
        char fingerprint_char[fingerprint_length];
        fingerprint.toCharArray(fingerprint_char, fingerprint_length);

//        client->setFingerprint(fingerprint_char);
    }

    DynamicJsonDocument doc(1024);

    doc["state"] = value;
    doc["attributes"]["unit_of_measurement"] = unit;
    doc["attributes"]["device_class"] = device_class;
    doc["attributes"]["attribution"] = "Data provided by NodeMCU";

    String data = "";
    serializeJson(doc, data);

    String url = SECRET_HA_BASE_URL "/api/states/" + entity_id;
    https.begin(client, url);
    https.addHeader("Authorization", String("Bearer ") + String(SECRET_TOKEN));
    https.addHeader("content-type", "application/json");
    
    Serial.print("Updating entity: ");
    Serial.print(entity_id);
    Serial.print(" at: ");
    Serial.println(url);

    int httpCode = https.POST(data);
    String payload = https.getString();
    Serial.println(payload);

    https.end();
}

void measure()
{
    int moisture_value = analogRead(moisturepin);
    moisture_humidity = map(moisture_value, wet, dry, 100.0f, 0.0f);

    humidity = dht.readHumidity();
    temperature = dht.readTemperature();
    heatindex = dht.computeHeatIndex(temperature, humidity, false);
    movement_detected = digitalRead(pirSensor) == 1;

    moisture_humidity_formatted = String(moisture_humidity, 1);
    humidity_formatted = String(humidity, 1);
    temperature_formatted = String(temperature, 1);
    heatindex_formatted = String(heatindex, 1);
    movement_formatted = movement_detected == 1 ? "<i class=\"fas fa-check\" style=\"color:#14ab00;\"></i>" : "<i class=\"fas fa-ban\" style=\"color:#f03232;\"></i>";

    send_data("plant.temperature", "temperature", temperature, "°C");
    send_data("plant.heatindex", "temperature", heatindex, "°C");
    send_data("plant.humidity", "humidity", humidity, "%");
    send_data("plant.moisture_humidity", "humidity", moisture_humidity, "%");
}

void check_for_update()
{
  
    currentTime = millis();

    if (currentTime - previousUpdateTime >= updateTime || previousUpdateTime == 0) {
        measure();
        previousUpdateTime = currentTime;
    }

    if (currentTime - previousPumpHighTime >= maxPumpTime) {
        digitalWrite(pumpPin, HIGH);      
        previousPumpHighTime = currentTime;
        pumpState = "off";
    }
}

// Rename this to loop (and the above function to something else) to run the webserver.
void loop()
{
    check_for_update();
    
    WiFiClient client = server.available();

    if (client)
    {
        Serial.println("New Client.");

        String currentLine = "";
        currentTime = millis();
        previousTime = currentTime;

        while (client.connected() && currentTime - previousTime <= timeoutTime)
        {
            currentTime = millis();

            if (client.available())
            {
                char c = client.read();
                HTTPheader += c;

                if (c == '\n')
                {
                    // if the byte is a newline character
                    // if the current line is blank, you got two newline characters in a row.
                    // that's the end of the client HTTP request, so send a response:
                    if (currentLine.length() == 0)
                    {
                        bool is_script = HTTPheader.indexOf("GET /index.js") >= 0;
                        client.println("HTTP/1.1 200 OK");
                        
                        if (is_script)
                            client.println("Content-type:application/javascript");
                        else
                            client.println("Content-type:text/html");
                            
                        client.println("Connection: close");
                        client.println();
                        
                        if (HTTPheader.indexOf("GET /data") >= 0)
                        {
                            client.println(temperature_formatted);
                            client.println(humidity_formatted);
                            client.println(moisture_humidity_formatted);
                            client.println(movement_formatted);
                            client.println(heatindex_formatted);
                            break;
                        } else if (HTTPheader.indexOf("GET /index.js") >= 0)
                        {
                            client.println(String(index_js));
                            break;
                        }
                        
                        // Turns the GPIOs on and off
                        if (HTTPheader.indexOf("GET /5/on") >= 0)
                        {
                            Serial.println("GPIO 5 on");
                            pumpState = "on";
                            digitalWrite(pumpPin, LOW);
                            previousPumpHighTime = millis();
                        }
                        else if (HTTPheader.indexOf("GET /5/off") >= 0)
                        {
                            Serial.println("GPIO 5 off");
                            pumpState = "off";
                            digitalWrite(pumpPin, HIGH);
                        }
                        
                        String button_html = pumpState != "on" ? "<p><a href=\"/5/on\"><button class=\"button\">TURN ON</button></a></p>" : "<p><a href=\"/5/off\"><button class=\"button button2\">TURN OFF</button></a></p>";

                        String html = String(index_html);
                        html.replace("%TEMPERATURE%", temperature_formatted);
                        html.replace("%HUMIDITY%", humidity_formatted);
                        html.replace("%MOISTURE%", moisture_humidity_formatted);
                        html.replace("%MOVEMENT%", movement_formatted);
                        html.replace("%LED_BUTTON%", button_html);

                        client.println(html);
                        break;
                    }
                    else
                        currentLine = "";
                }
                else if (c != '\r')
                {
                    // if you got anything else but a carriage return character,
                    // add it to the end of the currentLine
                    currentLine += c;
                }
            }
        }

        HTTPheader = "";
        client.stop();
    }
}
