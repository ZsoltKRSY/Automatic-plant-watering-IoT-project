#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ESP8266WebServer.h>
#include <KickSort.h>

#define NO_SAMPLES 10
#define SEC_IN_H 3600
#define DELAY_TIME 1000
#define WATERING_TIME 5 //in seconds

const char* ssid = "***";
const char* password = "***";

ESP8266WebServer server(80);

String page =
R"(
<html>  
  <head>
    <title>Automatic Irrigation System</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link rel='stylesheet' href='https://use.fontawesome.com/releases/v5.1.0/css/all.css'/>
    <link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.1.2/css/bootstrap.min.css'/>
    <script src='https://code.jquery.com/jquery-3.3.1.min.js'></script>
  </head>

  <body>
    <div class='container-fluid text-center'>
      <h2>Automatic Irrigation System</h2>

      <table class="table mx-auto" style="font-size:20px; width: auto;">
        <tr>  
          <td class="text-left pr-3">
            <div><i class="fa fa-thermometer-half" aria-hidden="true"></i> Temperature: </div>
            <div><i class="fa fa-cloud" aria-hidden="true"></i> Air relative humidity: </div>
            <div><i class="fa fa-tint" aria-hidden="true"></i> Soil moisture level: </div>
          </td>
          <td class="text-left pr-3">
            <div id='tempMeasured'></div>
            <div id='humMeasured'></div>
            <div id='soilMeasuredConverted'></div>
          </td>
        </tr> 
      </table>

      <div class='mt-4'>
        <h3>Time since last watering:</h3>
        <div class='d-flex justify-content-center align-items-center mt-3'>
          <div id="days" class="px-3 py-1 border rounded bg-light"></div>
          <div id="hours" class="px-3 py-1 border rounded bg-light"></div>
          <div id="minutes" class="px-3 py-1 border rounded bg-light"></div>
          <div id="seconds" class="px-3 py-1 border rounded bg-light"></div>
        </div>

        <label for='valueSlider'><strong>Set Watering Interval:</strong></label>
        <input type='range' id='valueSlider' min='8' max='72' step='1' value='12' class='custom-range' style='width: 300px;'>
        <div>(Watering takes place only if Soil moisture level is below 0.001%)</div>
        <div>Selected Interval: <span id='sliderValue'>12</span> hours</div>
      </div>

      <button class='btn btn-primary mt-3' onclick='sendSliderValue()'>Set Interval</button>
    </div>
  </body>
  
  <script> 
   $(document).ready(function(){ 
     setInterval(refreshFunction, 2000); 

     $('#valueSlider').on('input', function(){
      $('#sliderValue').text($(this).val());
     });
   });

   function refreshFunction(){
      $.get('/refresh', function(result){
        let values = result.split(",");
        if(values.length === 4) {
          $('#tempMeasured').html(values[0] + "*C");
          $('#humMeasured').html(values[1] + "%");
          $('#soilMeasuredConverted').html(values[2] + "%");

          let lastWateredSeconds = parseInt(values[3]);

          let days = Math.floor(lastWateredSeconds / 86400);
          let hours = Math.floor((lastWateredSeconds % 86400) / 3600);
          let minutes = Math.floor((lastWateredSeconds % 3600) / 60);
          let seconds = lastWateredSeconds % 60;

          $('#days').html(days + " days");
          $('#hours').html(hours + " hours");
          $('#minutes').html(minutes + " minutes");
          $('#seconds').html((seconds) + " seconds");
        }
        else{
          console.error("Unexpected response format:", result);
        }
      }).fail(function() {
        console.error("Failed to fetch /refresh");
      });
    }

    function sendSliderValue(){
      let sliderVal = $('#valueSlider').val();

      $.get('/set_interval?value=' + sliderVal, function(response){
        console.log("Response:", response);
      }).fail(function(){
        console.error("Failed to send interval value:", sliderVal);
      });
    }

  </script> 
</html> 
)";

unsigned long lastTime = 0;
unsigned long lastTimeSoil = DELAY_TIME;
unsigned long delayTimeSoil = 1000 * SEC_IN_H;

const int dht_pin = D6;
DHT_Unified dht(dht_pin, DHT11);

const int soilSensor_power_pin = D7;
const int soilSensor_pin = A0;

const int waterPump_pin = D3;

float tempMeasured = 0.0f;
float humMeasured = 0.0f;
float soilMeasuredConverted = 0.0f;

unsigned int wateringInterval = 12; //in hours 
unsigned long lastWatered = 0; //in seconds; 12 * SEC_IN_H - 60
unsigned int wateringTime = WATERING_TIME; //in seconds

bool wateringCond = false;

void handleRefresh(){
  char dataSend[100];
  sprintf(dataSend, "%.1f,%.1f,%.3f,%lu", tempMeasured, humMeasured, soilMeasuredConverted, lastWatered);

  server.send(200, "text/plain", dataSend);
}

void handleSetInterval() {
  if (server.hasArg("value")) {
    wateringInterval = server.arg("value").toInt();

    server.send(200, "text/plain", "Interval set to " + String(wateringInterval));
  } else {
    server.send(400, "text/plain", "Missing value");
  }
}

void htmlIndex(){
  server.send(200, "text/html", page);
}

void connectToWiFi(){
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);

  WiFi.begin(ssid, password); //192.168.0.164

  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println(WiFi.localIP());
}

void setupServer(){
  server.on("/", htmlIndex);
  server.on("/refresh", handleRefresh);
  server.on("/set_interval", handleSetInterval);

  server.begin();
}

void getDhtMeasurements(){
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if(isnan(event.temperature)){
      Serial.println("Error reading temperature!");
  }
  tempMeasured = event.temperature;

  dht.humidity().getEvent(&event);
  if(isnan(event.temperature)){
      Serial.println("Error reading humidity!");
  }
  humMeasured = event.relative_humidity;
}

void getSoilMeasurements(){
  uint16_t soilMeasured[NO_SAMPLES]; //we take 10 measurements of the soil moisture level every 1h and take the median value for best results
  for(int i = 0; i < NO_SAMPLES; ++i){
    soilMeasured[i] = analogRead(soilSensor_pin);
  }
  KickSort<uint16_t>::quickSort(soilMeasured, NO_SAMPLES, KickSort_Dir::ASCENDING);
  soilMeasuredConverted = 1.0f - soilMeasured[NO_SAMPLES / 2] / 1024.0f;
}

void setup() {
  Serial.begin(115200);

  pinMode(dht_pin, INPUT);
  pinMode(soilSensor_pin, INPUT);
  pinMode(waterPump_pin, OUTPUT);
  pinMode(soilSensor_power_pin, OUTPUT);
  digitalWrite(soilSensor_power_pin, LOW);

  connectToWiFi();
  setupServer();

  dht.begin();
}

void loop() {
  if((millis() - lastTimeSoil + DELAY_TIME) > delayTimeSoil){
    digitalWrite(soilSensor_power_pin, HIGH); //we turn the sensor on 1s before we take measurements so the capacitor can charge
    //the sensor shouldn't be powered all the time as electrolysis corrodes it quickly (as it is a resistive sensor)
  }

  if((millis() - lastTimeSoil) > delayTimeSoil){
    lastTimeSoil = millis();
    getSoilMeasurements();
    digitalWrite(soilSensor_power_pin, LOW);
  }

  if((millis() - lastTime) > DELAY_TIME){
    lastTime = millis();

    server.handleClient();

    getDhtMeasurements();

    ++lastWatered;
    if(lastWatered >= wateringInterval * SEC_IN_H){
      if(soilMeasuredConverted < 0.001f){
        //Serial.println("Started watering.");

        lastWatered = 0;
        wateringTime = WATERING_TIME;
        digitalWrite(waterPump_pin, HIGH);
        wateringCond = true;
      }
    }

    if(wateringCond){
      if(wateringTime <= 0){
        //Serial.println("Stopped watering.");
        digitalWrite(waterPump_pin, LOW);
        wateringCond = false;
      }
      else{
        //Serial.println("Watering...");
        --wateringTime;
      }
    }
  }
}