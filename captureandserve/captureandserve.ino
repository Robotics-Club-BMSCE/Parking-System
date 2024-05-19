#include "esp_camera.h"
#include "FS.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include "soc/soc.h"           // Brownout detector
#include "soc/rtc_cntl_reg.h"  // Brownout detector
#include <ESPAsyncWebServer.h>

// Create a WebServer object listening on port 80
AsyncWebServer server(80);

// Pin definition for CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Wi-Fi credentials
const char* ssid = "81NE-S010404 4362";
const char* password = "9054Nw&0";

// Files storage related declarations
unsigned long lastCaptureTime = 0;
unsigned int image_count = 0;
const int max_images = 6;
String image_files[max_images];  // Array to keep track of image filenames

void listFilesInDir(const char * dirname, uint8_t levels) {
  File root = SPIFFS.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listFilesInDir(file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

String listFiles() {
  String fileList = "";
  File root = SPIFFS.open("/");
  if (!root) {
    Serial.println("Failed to open directory");
    return fileList;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return fileList;
  }
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      fileList += "<a href=\"";
      fileList += file.name();
      fileList += "\">";
      fileList += file.name();
      fileList += "</a><br>";
    }
    file = root.openNextFile();
  }
  return fileList;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);
  delay(1000); // Allow some time for the Serial Monitor to initialize
  
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error occurred while mounting SPIFFS");
    return;
  } else {
    Serial.println("SPIFFS mounted successfully");
  }

  // Print SPIFFS contents to Serial Monitor
  listFilesInDir("/", 0);

  // Initialize the camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; 

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;  // Reduced frame size
    config.jpeg_quality = 12;  // Higher quality
    config.fb_count = 2;
  } 
  else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    switch (err) {
      case ESP_ERR_NOT_FOUND:
        Serial.println("Camera module not found");
        break;
      case ESP_ERR_INVALID_STATE:
        Serial.println("Invalid state. Camera already initialized");
        break;
      case ESP_ERR_NO_MEM:
        Serial.println("Failed to allocate memory");
        break;
      case ESP_ERR_INVALID_ARG:
        Serial.println("Invalid argument");
        break;
      default:
        Serial.println("Unknown error");
        break;
    }
    return;
  } 
  else {
    Serial.println("Camera initialized successfully");
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(100);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi");
    return;
  } 
  else {
    Serial.println("Connected to WiFi");
    Serial.println(WiFi.localIP());
  }

  // Serve index page that lists all files
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String fileList = "<h2>Files in SPIFFS</h2>";
    fileList += listFiles();
    request->send(200, "text/html", fileList);
  });

  // Serve files
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // Start server
  server.begin();

  Serial.println("Server started");

  // Print initial heap size
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
}

void loop() {
  // Capture and save an image every 30 seconds
  if (millis() - lastCaptureTime >= 30000) {
    captureAndSaveImage();
    lastCaptureTime = millis();
  }
}

void captureAndSaveImage() {
  // Capture an image
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  // Create a unique file name with the image count
  char filename[30];
  snprintf(filename, sizeof(filename), "/image_%d.jpg", image_count);

  // Open file in write mode
  File file = SPIFFS.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file in writing mode");
    esp_camera_fb_return(fb);
    return;
  }

  // Write data to file
  if (file.write(fb->buf, fb->len) != fb->len) {
    Serial.println("File write failed");
    file.close();
    esp_camera_fb_return(fb);
    return;
  }

  Serial.printf("Saved file to path: %s of size %u\n", filename, fb->len);
  image_files[image_count % max_images] = String(filename);
  image_count++;

  // Delete the oldest image if max_images is exceeded
  if (image_count > max_images) {
    String oldest_file = image_files[image_count % max_images];
    if (SPIFFS.remove(oldest_file)) {
      Serial.printf("Deleted file: %s\n", oldest_file.c_str());
    } else {
      Serial.printf("Failed to delete file: %s\n", oldest_file.c_str());
    }
  }

  // Close the file
  file.close();
  esp_camera_fb_return(fb);
}
