/*********
  Based on the following source by Rui Santos: http://randomnerdtutorials.com
*********/

// Load Wi-Fi library
#include <ESP8266WiFi.h>
#include <TM1637.h>
#include "DHT.h"
#include "arduino_secrets.h"

// Digital pins
const int DHTPIN = D1;
const int pirSensor = D3;
const int ledout = D4;
const int CLK = D8;
const int DIO = D7;

// Analog pins
int moisturepin = A0;

const int DHTTYPE = DHT22; // DHT 22  (AM2302)

TM1637 tm(CLK, DIO);
WiFiServer server(80);
DHT dht(DHTPIN, DHTTYPE);

// Variable to store the HTTP request
String HTTPheader;

String ledoutState = "off";

unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000; // ms

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

void setup()
{
    Serial.begin(115200);

    tm.init();
    tm.set(BRIGHT_TYPICAL);

    dht.begin();

    pinMode(ledout, OUTPUT);
    pinMode(moisturepin, INPUT);

    digitalWrite(ledout, LOW);

    // Connect to Wi-Fi network with SSID and password
    Serial.print("Connecting to ");
    Serial.println(SECRET_SSID);
    WiFi.begin(SECRET_SSID, SECRET_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    // Print local IP address and start web server
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    server.begin();

    // TODO: Change to something useful (such as the temperature)
    tm.display(0, 0);
    tm.display(1, 1);
    tm.display(2, 2);
    tm.display(3, 3);
}

int wet = 368;
int dry = 762;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
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
  %LEDBUTTON%
</body>
<script>
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
}, 10000 ) ;
</script>
</html>)rawliteral";

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
    movement_formatted = movement_detected == 1 ?
        "<i class=\"fas fa-check\" style=\"color:#14ab00;\"></i>" :
        "<i class=\"fas fa-ban\" style=\"color:#f03232;\"></i>";
}

void loop()
{
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
                Serial.write(c);
                HTTPheader += c;

                if (c == '\n')
                {
                    // if the byte is a newline character
                    // if the current line is blank, you got two newline characters in a row.
                    // that's the end of the client HTTP request, so send a response:
                    if (currentLine.length() == 0)
                    {
                        client.println("HTTP/1.1 200 OK");
                        client.println("Content-type:text/html");
                        client.println("Connection: close");
                        client.println();

                        // Turns the GPIOs on and off
                        if (HTTPheader.indexOf("GET /4/on") >= 0)
                        {
                            Serial.println("GPIO 4 on");
                            ledoutState = "on";
                            digitalWrite(ledout, HIGH);
                        }
                        else if (HTTPheader.indexOf("GET /4/off") >= 0)
                        {
                            Serial.println("GPIO 4 off");
                            ledoutState = "off";
                            digitalWrite(ledout, LOW);
                        }
                        else if (HTTPheader.indexOf("GET /data") >= 0)
                        {
                            measure();
                            client.println(temperature_formatted);
                            client.println(humidity_formatted);
                            client.println(moisture_humidity_formatted);
                            client.println(movement_formatted);
                            client.println(heatindex_formatted);
                        }
                        else
                        {
                            String button_text = ledoutState == "on" ?
                              "<p><a href=\"/4/on\"><button class=\"button\">ON</button></a></p>" :
                              "<p><a href=\"/4/off\"><button class=\"button button2\">OFF</button></a></p>";

                            String html = String(index_html);
                            html.replace("%TEMPERATURE%", temperature_formatted);
                            html.replace("%HUMIDITY%", humidity_formatted);
                            html.replace("%MOISTURE%", moisture_humidity_formatted);
                            html.replace("%MOVEMENT%", movement_formatted);
                            html.replace("%LED_BUTTON%", movement_formatted);

                            client.println(html);
                        }
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

        Serial.println("Client disconnected.");
        Serial.println("");
    }
}
