//libraries
#include <Arduino.h>
#include <ArduinoOTA.h>
#include "ESP8266WiFi.h"
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <math.h>
#include <LittleFS.h>
#include <FS.h>

//project files
#include "pixels.h"
#include "oled.h"
#include "clock.h"
#include "defaultgif.c"
#include "wifiglyph.c"

// brightness=20, 256 leds --> 1A
// brightness=127, 256 leds --> 5A

// button: D2 = GPIO4
#define BUTTON_PIN           4
#define OTA_PASSWORD         "1234"
#define UDP_PORT             1234
#define AP_NETWORK           "rgbled"

#define ANIMATION_NONE       0
#define ANIMATION_MATRIX     1
#define ANIMATION_RAINBOW    2
#define ANIMATION_STARS      3
#define ANIMATION_SCROLL     4
#define ANIMATION_CLOCK      5
#define ANIMATION_DEFAULTGIF 6
#define ANIMATION_CUSTOMGIF  7
#define ANIMATION_SUNRISE    8

#define ANIMATIONS_COUNT     8
#define DEFAULT_ANIMATION    ANIMATION_DEFAULTGIF

#define STARS_COUNT          150
#define MAX_SCROLL_TEXT      1000
#define MAX_UPLOAD_SIZE      1572864
#define AUTO_ANIMATION_SECS  60

#define CUSTOMGIF_FILE       "/custom.bin"
#define MEMORY_FILE          "/memory.dat"
#define SCROLL_FILE          "/scroll.txt"
#define TMP_FILE             "/tmp.txt"
#define WIFI_FILE            "/wifi.txt"

#define RGB(r,g,b) ( ((uint32_t(r))<<16) + ((uint32_t(g)) <<8 ) + (uint32_t(b)) )

ESP8266WebServer server(80);
ADC_MODE(ADC_VCC);
WiFiUDP Udp;

const char compile_date[] = __DATE__ " " __TIME__;
const char* week_days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char* week_days_short[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char* months_short[] = {"Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sept", "Oct",  "Nov", "Dec"};
unsigned long counter = 0;
unsigned long last_activity = 0;
int button_now = HIGH;
int button_prev = HIGH;
char small_buffer[256];
char large_buffer[2048];
char wifi_config[256];
char* wifi_ssid = NULL;
char* wifi_pass = NULL;
bool wifi_ap = false;
byte pixel_buffer[3*NUMPIXELS];
byte udp_buffer[UDP_TX_PACKET_MAX_SIZE + 1];
int ota_progress = 0;
double matrix_columns[WIDTH];
double matrix_speeds[WIDTH];
double matrix_ticks = 0;
byte matrix_h = 7;
byte matrix_colors[7] = {255,96,48,36,20,16,0};
double stars_x[STARS_COUNT];
double stars_y[STARS_COUNT];
double stars_z[STARS_COUNT];
int animation_start = 0;
char scroll_text[MAX_SCROLL_TEXT] = {0};
int animation = 0;
int prev_animation = DEFAULT_ANIMATION;
double animation_speed = 1.0;
int animation_brightness = 255;
File fsUploadFile;
long uploaded_so_far = 0;

struct memoryStruct {
    int animation;
    int brightness;
} memory;

void readMemory()
{
  memset((uint8_t*) &memory, 0, sizeof(memory));
  if (LittleFS.exists(MEMORY_FILE))
  { 
      File file = LittleFS.open(MEMORY_FILE, "r");
      file.read((uint8_t*) &memory, sizeof(memory));
      file.close();
  }
  else
  {
    memory.brightness = 20;
    memory.animation = ANIMATION_MATRIX;
    File file = LittleFS.open(MEMORY_FILE, "w");
    file.write((uint8_t*) &memory, sizeof(memory));
    file.close();
  }
}

void saveMemory()
{
  File file = LittleFS.open(MEMORY_FILE, "w");
  file.write((uint8_t*) &memory, sizeof(memory));
  file.close();
}

void saveWifiConfig(const char* ssid, const char* pass)
{
  File file = LittleFS.open(WIFI_FILE, "w");
  sprintf(wifi_config, "%s\n%s", ssid, pass);
  file.write((uint8_t*) &wifi_config, strlen(wifi_config));
  file.close();
}

void loadWifiConfig()
{
  wifi_ssid = NULL;
  wifi_pass = NULL;
  if (LittleFS.exists(WIFI_FILE))
  {
      File file = LittleFS.open(WIFI_FILE, "r");
      int len = file.read((uint8_t*) wifi_config, 256);
      file.close();
      if (len>0)
      {
        wifi_config[len] = 0;
        wifi_ssid = wifi_config;
        char* delimiter = strchr(wifi_config, '\n');
        if (delimiter) 
        {
          delimiter[0] = 0;
          wifi_pass = delimiter + 1;
        }
      }
  } 
}

void blink(int count, int ms)
{
  for (int i=0; i<count; i++)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(ms);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(ms);
  }
}

void system_scroll(int y, const char* text, uint32_t color)
{
    int dx = strlen(text)*6 + WIDTH;
    for(int x=WIDTH; x>-dx; x--)
    {    
      draw_text(x, y, 0, text, color);
      pixels.show();
      delay(2);
    }
}

size_t get_file_size(const char* filename) {
  auto file = LittleFS.open(filename, "r");
  size_t filesize = file.size();
  // Don't forget to clean up!
  file.close();
  return filesize;
}

void oledInfo(const char* line1, const char* line2, const char* line3)
{
  oledTextNoPurge(0, 16, "                     ");
  oledTextNoPurge(0, 24, "                     ");
  oledTextNoPurge(0, 32, "                     ");
  if (line1!=NULL && strlen(line1) > 0)
      oledTextNoPurge(0,16,line1);
  if (line2!=NULL && strlen(line2) > 0)
      oledTextNoPurge(0,24,line2);
  if (line3!=NULL && strlen(line3) > 0)
      oledTextNoPurge(0,32,line3);
  oledPurge();
}

void setupOTA()
{
 ArduinoOTA.onStart([]() {
    last_activity = millis();
    blink(5, 25);
    oledText(0, 8, "OTA start           ");
    pixels.clear();
    draw_text(7,4,0,"OTA",RGB(0, 128, 192));
    pixels.show(); 
  });

  ArduinoOTA.onEnd([]() {  
    last_activity = millis();
    blink(3, 100);
    oledText(0, 8, "OTA done, restarting");
    pixels.clear();
    for(int x=0; x<WIDTH; x++) { 
      set_pixel(x, 15, 0, 255, 0);
      set_pixel(x, 16, 0, 255, 0);
    }
    draw_text(7,4,0,"OTA",RGB(0, 255, 0));
    draw_text(10,20,0,"OK",RGB(0, 255, 0));
    pixels.show(); 
  });

  ArduinoOTA.onError([](ota_error_t error) {
    last_activity = millis();
    oledText(0, 8, "OTA error           ");
    pixels.clear();
    for(int x=0; x<WIDTH; x++) { 
      set_pixel(x, 15, 255, 0, 0);
      set_pixel(x, 16, 255, 0, 0);
    }
    pixels.show(); 
    blink(10, 25);
    ESP.restart();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    last_activity = millis();
    long percent = (progress * 100l) / total;
    oledTextNoPurge(0, 8, "                      ");
    sprintf(small_buffer, "OTA %d%%", percent);
    oledText(0, 8, small_buffer);
    ota_progress++;
    digitalWrite(LED_BUILTIN, ota_progress % 2 == 0 ? LOW : HIGH);
    int bar = (progress * WIDTH) / total;
    set_pixel(bar, 15, 0, 128, 192);
    set_pixel(bar, 16, 0, 128, 192);
    sprintf(small_buffer, "%d%%", percent);
    draw_text(16-strlen(small_buffer)*3, 20, 0, small_buffer, RGB(0, 64, 96));
    pixels.show(); 
  });  

  ArduinoOTA.setHostname("rgbmatrix");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
}

void draw_number(int y, int n, uint32_t color)
{
  sprintf(small_buffer, "%d", n);
  draw_text(16-strlen(small_buffer)*3, y, 0, small_buffer, color);
}

void show_wifi_glyph(int frame_nr, uint32_t color)
{
  pixels.clear();
  for(int l=0; l<16; l++)
  {
      uint16_t line = pgm_read_word_aligned(&wifi_gif[l+16*frame_nr]);
      for(int x=0; x<16; x++)
          set_pixel(8+x,12+l,line & (1<<x) ? color : 0);
  }
  pixels.show(); 
}

void checkFactoryReset()
{
  int wait = millis();
  while((millis()-wait)/1000 < 6)
  {
    if (digitalRead(BUTTON_PIN) == HIGH){
      pixels.clear();
      pixels.show();
      return;
    }

    draw_text(1,4,0,"reset", RGB(0, 128, 255));
    draw_text(12,12,0,"in", RGB(0, 128, 255));
    sprintf(small_buffer, "%d s", 6-(millis()-wait)/1000);
    draw_text(9,20,0,small_buffer, RGB(0, 128, 255));
    pixels.show();
    delay(10);
  }

  if (digitalRead(BUTTON_PIN) == LOW)
  {
    pixels.clear();
    system_scroll(12, "Resetting network settings", RGB(255, 255, 0));
    LittleFS.remove(WIFI_FILE);
  }

  pixels.clear();
  pixels.show();
}

void setupWifi() 
{
  oledText(0,16,"WIFI...");
  show_wifi_glyph(0, RGB(0,0,255));

  loadWifiConfig();
  if (wifi_ssid && wifi_pass)
  {
    wifi_ap = false;
    WiFi.mode(WIFI_STA);
    WiFi.setPhyMode(WIFI_PHY_MODE_11G);
    wl_status_t s = WiFi.begin(wifi_ssid, wifi_pass); 
    Serial.println(s);
    int i = 0;
    while (WiFi.status() != WL_CONNECTED) { 
      Serial.print(WiFi.status());
      delay(100);
      digitalWrite(LED_BUILTIN, i % 2 == 0 ? LOW : HIGH);
      show_wifi_glyph(3-(i%3), RGB(0,0,255));
      i++;
      if (i%300 == 0) {
        pixels.clear();
        sprintf(large_buffer, "Error connecting to WiFi '%s'. Consider restarting holding the button to reconfigure.", wifi_ssid);
        system_scroll(12, large_buffer, RGB(255,0,0));
        pixels.clear();
      }
    }
    show_wifi_glyph(0, RGB(0,255,0));
    oledText(50,16,WiFi.localIP().toString().c_str());
    delay(200);
    pixels.clear();
    system_scroll(12, WiFi.localIP().toString().c_str(), RGB(0,255,0));
    pixels.clear();
    
  } 
  else
  {
    wifi_ap = true;
    show_wifi_glyph(0, RGB(255,255,0));
    delay(200);
    if (!WiFi.softAP(AP_NETWORK)) {
      pixels.clear();
      system_scroll(12, "Error starting WiFi AP.", RGB(255,0,0));
    } else {
      pixels.clear();
      sprintf(large_buffer, "Connect to 'rgbled' and to go http://%s", WiFi.softAPIP().toString().c_str());
      system_scroll(12,large_buffer, RGB(128,255,0));
    }

    oledText(50,16,WiFi.softAPIP().toString().c_str());
  }

  Udp.begin(UDP_PORT);
  start_clock();
}

long get_free_space() {
  FSInfo fs_info;
  LittleFS.info(fs_info);
  return fs_info.totalBytes - fs_info.usedBytes;
}

void startAnimationText(const char* txt)
{
  pixels.clear();
  draw_text(16-strlen(txt)*3, 8, 0, txt, RGB(0,255,0));
  pixels.show();
  delay(300);
}

void startAnimation(int nr)
{
  memory.animation = nr;
  saveMemory();
  animation =  nr;
  animation_start = counter;
  if (animation == ANIMATION_MATRIX)
  {
    startAnimationText("mtrix");
    oledInfo("animation: matrix", NULL, NULL);
    matrix_ticks = 30;
    for(int x=0; x<WIDTH; x++)
    {
      matrix_columns[x] = - matrix_h - double(random(2*HEIGHT*100)) / 100.0f;
      matrix_speeds[x] = 0.25f + double(random(1000)) / 2000.0f;
    }
  } 
  else if (animation == ANIMATION_RAINBOW)
  {
    startAnimationText("rnbow");
    oledInfo("animation: rainbow", NULL, NULL);
  }
  else if (animation == ANIMATION_STARS)
  {
    startAnimationText("stars");
    oledInfo("animation: stars", NULL, NULL);
    for(int s=0; s<STARS_COUNT; s++)
    {
        stars_x[s] = (20*WIDTH - random(40*WIDTH)) / 10.0;
        stars_y[s] = random(HEIGHT);
        stars_z[s] = 0.1+random(90)/100.0;
    }
  }
  else if (animation == ANIMATION_SCROLL)
  {
    startAnimationText("scrll");
    oledInfo("animation: scroll", NULL, NULL);
    if (LittleFS.exists(SCROLL_FILE))
    {
      File file = LittleFS.open(SCROLL_FILE, "r");
      int len = file.read((uint8_t*) &scroll_text, MAX_SCROLL_TEXT-1);
      scroll_text[len] = 0;
      file.close();
    }
    else
    {
      strcpy(scroll_text, "This is default text, use GUI or API to set your.");
    }
  } 
  else if (animation == ANIMATION_CLOCK) 
  {
    startAnimationText("clock");
    oledInfo("animation: clock", NULL, NULL);
  }
  else if (animation == ANIMATION_DEFAULTGIF)
  {
    startAnimationText("gif1");
    oledInfo("animation: defaultgif", NULL, NULL);
  }
  else if (animation == ANIMATION_CUSTOMGIF)
  {
    startAnimationText("gif2");
    sprintf(small_buffer, "size:%db", get_file_size(CUSTOMGIF_FILE));
    oledInfo("animation: customgif", small_buffer, NULL);
  }
  else if (animation == ANIMATION_SUNRISE)
  {
    startAnimationText("sunr");
    oledInfo("animation: sunrise", NULL, NULL);
  }
  else
  {
    startAnimationText("none");
    oledInfo("animation: none", NULL, NULL);
  }

  pixels.clear();
  pixels.show();
}

void animate()
{
  if (animation == ANIMATION_MATRIX)
  {
    for(int x=0;x<WIDTH;x++)
    {
      double max_y = matrix_columns[x] + matrix_ticks*matrix_speeds[x];
      for(int c=0; c<matrix_h; c++)
      {
        int sy = lround(max_y-c);
        if (sy>=0 && sy<HEIGHT)
          set_pixel(x, sy, 0, (matrix_colors[c] * animation_brightness) / 255, 0);     
      }  
      
      if (max_y > HEIGHT + matrix_h + 1)
        matrix_columns[x] = matrix_columns[x] - random(2*HEIGHT) - HEIGHT - matrix_h;
    }

    pixels.show();
    matrix_ticks += (0.3 * animation_speed);
  } 
  else if (animation == ANIMATION_RAINBOW)
  {
    for(int x=0; x<WIDTH; x++)
    {
      for(int y=0; y<HEIGHT; y++)
      {
        int first = (lround((counter-animation_start) * animation_speed) * (WIDTH+HEIGHT)) % 65536;
        uint16_t hue = first + ((x+y) * 65536) / (WIDTH+HEIGHT);
        uint32_t color = Adafruit_NeoPixel::ColorHSV(hue, 255, animation_brightness);
        color = Adafruit_NeoPixel::gamma32(color);
        set_pixel(x, y, color);  
      }
    }

    pixels.show();
  } 
  else if (animation == ANIMATION_STARS) 
  {
    pixels.clear();
    for(int s=0; s<STARS_COUNT; s++)
    {
        double t = counter - animation_start;
        double x = stars_x[s] - t * stars_z[s]*stars_z[s]*3;
        if (x < 0)
            stars_x[s] = stars_x[s] + 3*WIDTH;
        double y = stars_y[s];
        int c = round(255*stars_z[s]*stars_z[s]*stars_z[s]*stars_z[s]);
        if (c < 20)
            c = 20;
        if (c > 255)
            c = 255;
        int rx = round(x);
        int ry = round(y);
        set_pixel(rx, ry, c, c, c); 
    }
    pixels.show();
  } 
  else if (animation == ANIMATION_SCROLL) 
  { 
    int font_nr = 3;
    int dx = get_text_width(get_font_ptr(font_nr), scroll_text) + WIDTH;
    int x = WIDTH - (counter - animation_start) % dx;
    draw_text(x, 1, font_nr, scroll_text, -1);
    pixels.show();
  }
  else if (animation == ANIMATION_CLOCK)
  {
    pixels.clear();
    long local = get_local_time();
    sprintf(small_buffer, "%02d%c%02d", hour(local), second(local) % 2 == 0 ? ':' : ' ', minute(local));
    draw_text(2, 0, 0, small_buffer, RGB(0,255,0));
    const char* txt = week_days_short[(weekday(local)+6)%7];
    draw_text(16-strlen(txt)*3, 8, 0, txt , RGB(255,255,0));
    txt = months_short[month(local)-1];
    draw_text(16-strlen(txt)*3, 16, 0, txt , RGB(255,255,0));
    sprintf(small_buffer, "%02d", day(local));
    draw_text(12, 24, 0, small_buffer, RGB(255,255,0));
    pixels.show();
  }
  else if (animation == ANIMATION_DEFAULTGIF)
  {
    pixels.clear();
    int progmem_size = sizeof(default_gif)/sizeof(default_gif[0]);
    int frame_size = WIDTH*HEIGHT*3;
    int frame_count = progmem_size / frame_size;
    int frame_nr = counter % frame_count;
    int frame_offset = frame_nr*frame_size;
    for(int y=0; y<HEIGHT; y++)
    {
      for(int x=0; x<WIDTH; x++)
      {
        int index = frame_offset + ((y<<5) + x)*3;
        int r = pgm_read_byte(&default_gif[index]);
        int g = pgm_read_byte(&default_gif[index+1]);
        int b = pgm_read_byte(&default_gif[index+2]);
        set_pixel(x,y,r,g,b);
      }
    }
 
    delay(20);
    pixels.show();
  }
  else if (animation == ANIMATION_CUSTOMGIF)
  {
    pixels.clear();
    uint8_t color[3];
    File file = LittleFS.open(CUSTOMGIF_FILE, "r");
    size_t filesize = file.size();
    int frame_size = (WIDTH*HEIGHT*3);
    int frame_count = filesize / frame_size;
    int current_frame = (counter % frame_count);
    int offset = current_frame * frame_size;
    file.seek(offset);
    for(int y=0; y<HEIGHT; y++)
    {
      for(int x=0; x<WIDTH; x++)
      {
        file.read((uint8_t*)&color, 3);
        set_pixel(x,y,color[0],color[1],color[2]);
      }
    }
    
    file.close();
    pixels.show();
  }
  else if (animation == ANIMATION_SUNRISE)
  {
      int duration = 512*15; //512=60s 
      float half_duration = duration/2.0;
      float t = (counter-animation_start)*1.0;
      float radius = 20.0;
      float cx = 16;
      float cy = 16;
      if (t < half_duration)
      {
          cy = 48.0 - t/(half_duration/32.0);
          radius = 20.0;
      }
      else if (t < duration)
      {
          cy = 16.0;
          radius = 20.0 + 10.0 * (t - half_duration) / half_duration;
      }
      else
      {
          cy = 16.0;
          radius = 30.0 + 5*sin((t-duration)/10.0);
      }
      
      for(int32_t x=0; x<WIDTH;x++)
      {
        for(int32_t y=0; y<HEIGHT; y++)
        {
          float d = sqrt((x-cx)*(x-cx) + (y-cy)*(y-cy));  
          float f = 2.0 - (d / (radius/2));
          if (f >= 0)
          {
            int32_t b = 255 * (f*f) / 4;

            if (b < 0)
              b = 0;
            if (b > 255)
              b = 255;


            uint32_t hue = ( ((28-y) * 65536 + 65536) / (5*HEIGHT))%65536;
            uint32_t color = Adafruit_NeoPixel::ColorHSV(hue, 255, b);
            set_pixel(x, y, color);
          }
          else
          {
            set_pixel(x, y, 0);
          }
        }
      }

      pixels.show();
  }
}

void stopAnimation()
{
  if (animation > 0)
    prev_animation = animation;
  animation = 0;
  oledInfo(NULL, NULL, NULL);
}

uint32_t get_request_color()
{
  if (server.hasArg("color"))
    return server.arg("color").toInt();
  
  int r = 255;
  int g = 255;
  int b = 255;
  if (server.hasArg("r"))
    r = server.arg("r").toInt();
  if (server.hasArg("g"))
    g = server.arg("g").toInt();
  if (server.hasArg("b"))
    b = server.arg("b").toInt();
  return RGB(r,g,b);
}

void webResponse(int httpStatus, const char* status, const char* msg) {
  sprintf(large_buffer, "{\"status\":\"%s\", \"msg\":\"%s\"}", status, msg);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(httpStatus, "application/json", large_buffer);
  oledText(0,40,"                     ");
  oledText(0,40,msg);
}

void setupWebServer()
{
  server.on("/",  HTTP_GET, [] {
    blink(1, 50);
    last_activity = millis();
    File file = LittleFS.open("/index.html" , "r");
    server.streamFile(file, "text/html");
    file.close(); 
  });

  server.on("/status",  HTTP_GET, [] {
    blink(1, 1);
    long local = get_local_time();
    sprintf(small_buffer, "%04d-%02d-%02d %02d:%02d:%02d", year(local), month(local), day(local), hour(local), minute(local), second(local));
    sprintf(large_buffer, "{ \"vcc\":%d, \"rssi\":%d, \"storage\":%d, \"heap\":%d, \"core\":\"%s\", \"sdk\":\"%s\", \"app\":\"%s\", \"time\":\"%s\", \"uptime\":%d, \"anim\":%d, \"b\":%d, \"wifi\":\"%s\" }",ESP.getVcc(), WiFi.RSSI(), get_free_space(), ESP.getFreeHeap(), ESP.getCoreVersion(), ESP.getSdkVersion(), compile_date, small_buffer, millis()/1000, animation, memory.brightness, WiFi.SSID());
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", large_buffer); 
  }); 

  server.on("/pixel",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    stopAnimation();
    if (server.hasArg("x") && server.hasArg("y"))
    {
        int x = server.arg("x").toInt();
        int y = server.arg("y").toInt();
        set_pixel(x, y, get_request_color());
        pixels.show();
        oledInfo("pixel", NULL, NULL);
        sprintf(small_buffer, "x:%d y:%d", x, y);
        webResponse(200, "OK", small_buffer);
    } else {
      webResponse(400, "ERROR", "no x,y parameters");
    }
  });

  server.on("/rect",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    stopAnimation();
    if (server.hasArg("x") && server.hasArg("y") && server.hasArg("w") && server.hasArg("h"))
    {
        int x = server.arg("x").toInt();
        int y = server.arg("y").toInt();
        int w = server.arg("w").toInt();
        int h = server.arg("h").toInt();
        draw_rect(x, y, w, h, get_request_color());
        pixels.show();
        oledInfo("rect", NULL, NULL);
        sprintf(small_buffer, "x:%d y:%d w:%d, h:%d", x, y, w, h);
        webResponse(200, "OK", small_buffer);
    } else {
      webResponse(400, "ERROR", "no x,y,w,h parameters");
    }
  });

  server.on("/clear",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    stopAnimation();
    pixels.clear();
    for(int i=0; i<NUMPIXELS; i++)
      pixels.setPixelColor(i, get_request_color());
    pixels.show();
    sprintf(small_buffer, "clear");
    webResponse(200, "OK", small_buffer);
  });

  server.on("/settings",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    if (server.hasArg("brightness")) {
          memory.brightness = server.arg("brightness").toInt();
          pixels.setBrightness(memory.brightness);
          saveMemory();
          sprintf(small_buffer, "brightness=%d", memory.brightness);
          webResponse(200, "OK", small_buffer);
    } else {
        webResponse(400, "ERROR", "no parameters");
    }
  });

  server.on("/text",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    if (server.hasArg("text")) {
      stopAnimation();
      int x = 0;
      int y = 0;
      int font_nr = 0;
      uint32_t color = get_request_color();
      if (server.hasArg("x")) x = server.arg("x").toInt();
      if (server.hasArg("y")) y = server.arg("y").toInt();
      if (server.hasArg("font")) font_nr = server.arg("font").toInt();
      strcpy(large_buffer, server.arg("text").c_str());
      draw_text(x, y, font_nr, large_buffer, color);
      pixels.show();
      oledInfo("text", NULL, NULL);
      sprintf(small_buffer, "text:%d", strlen(large_buffer));
      webResponse(200, "OK", small_buffer);
    } else {
        webResponse(400, "ERROR", "no parameters");
    }
  });

  server.on("/effect",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    stopAnimation();
    animation_speed = 1.0;
    if (server.hasArg("speed")) {
        animation_speed = server.arg("speed").toFloat();
    }

    animation_brightness = 255;
    if (server.hasArg("brightness")) {
        animation_brightness = server.arg("brightness").toInt();
    }

    if (server.hasArg("name") && server.arg("name").equals("matrix")) {
        startAnimation(ANIMATION_MATRIX);          
    } else if (server.hasArg("name") && server.arg("name").equals("rainbow")) {
        startAnimation(ANIMATION_RAINBOW);       
    } else if (server.hasArg("name") && server.arg("name").equals("stars")) {
        startAnimation(ANIMATION_STARS);       
    } else if (server.hasArg("name") && server.arg("name").equals("scroll")) {
        if (server.hasArg("text") && strlen(server.arg("text").c_str()) > 0) 
        {
            scroll_text[0] = 0;
            strcpy(scroll_text, server.arg("text").c_str());
            File file = LittleFS.open(SCROLL_FILE, "w");
            file.print(scroll_text);
            file.close();
        }
        startAnimation(ANIMATION_SCROLL);       
    } else if (server.hasArg("name") && server.arg("name").equals("clock")) {
        startAnimation(ANIMATION_CLOCK);       
    } else if (server.hasArg("name") && server.arg("name").equals("defaultgif")) {
        startAnimation(ANIMATION_DEFAULTGIF);       
    } else if (server.hasArg("name") && server.arg("name").equals("customgif")) {
        startAnimation(ANIMATION_CUSTOMGIF);       
    } else if (server.hasArg("name") && server.arg("name").equals("sunrise")) {
        startAnimation(ANIMATION_SUNRISE);       
    }


    sprintf(small_buffer, "effect:%d", animation);
    webResponse(200, "OK", small_buffer);
  });

  server.on("/upload",  HTTP_POST, [](){}, [] {
    blink(1, 1);
    last_activity = millis();
    HTTPUpload& upload = server.upload();
    if (!upload.filename.equals("custom.bin")) {
        if (!server.hasArg("password")) {
          webResponse(403, "ERROR", "no password");
          return;
        }
        if (!server.arg("password").equals(OTA_PASSWORD)) {
          webResponse(403, "ERROR", "invalid password");
          return;
        }
        if (!upload.filename.equals("index.html")) {
          webResponse(400, "ERROR", "invalid file name");
          return;
        }
        if (upload.totalSize > MAX_UPLOAD_SIZE)
        {
          sprintf(small_buffer, "file size %d too big. Max is %d", upload.totalSize, MAX_UPLOAD_SIZE);
          webResponse(400, "ERROR", "invalid file name");
          return;
        }
    }

    if(upload.status == UPLOAD_FILE_START){
      oledInfo("uploading", upload.filename.c_str(), NULL);
      String filename = upload.filename;
      if(!filename.startsWith("/")) filename = "/"+filename;
      fsUploadFile = LittleFS.open(filename, "w");
      uploaded_so_far = 0;
    } else if(upload.status == UPLOAD_FILE_WRITE){
      if(fsUploadFile)
        fsUploadFile.write(upload.buf, upload.currentSize); 
        uploaded_so_far = uploaded_so_far + upload.currentSize;
        sprintf(small_buffer, "%d/%d", uploaded_so_far, upload.totalSize);
        oledInfo("uploading", upload.filename.c_str(), small_buffer);
    } else if(upload.status == UPLOAD_FILE_END){
      if(fsUploadFile) {                     
        sprintf(small_buffer, "%s:%d", upload.filename.c_str(), upload.totalSize);         
        fsUploadFile.close();           
        webResponse(200, "OK", small_buffer);
      } else {
        webResponse(500, "ERROR", "fatal_1");
      }
    } else {
      webResponse(500, "ERROR", "fatal_2");
    }

  });

  server.on("/reboot",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    stopAnimation();
    pixels.clear();
    pixels.show();
    sprintf(small_buffer, "reboot");
    webResponse(200, "OK", small_buffer);
    delay(100);
    ESP.restart();
  });

  server.on("/wifi",  HTTP_GET, [] {
    blink(1, 1);
    last_activity = millis();
    int numberOfNetworks = WiFi.scanNetworks();
    File file = LittleFS.open(TMP_FILE, "w");
    for(int i =0; i<numberOfNetworks; i++){
      sprintf(small_buffer, "%s\t%d\n", WiFi.SSID(i), WiFi.RSSI(i));
      file.print(small_buffer);
    } 
    file.close();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    file = LittleFS.open(TMP_FILE , "r");
    server.streamFile(file, "text/plain");
    file.close(); 
  });

  server.on("/wifi",  HTTP_POST, [] {
    blink(1, 1);
    last_activity = millis();
    if (server.hasArg("ssid") && server.hasArg("pass"))
    {
      saveWifiConfig(server.arg("ssid").c_str(), server.arg("pass").c_str());
      loadWifiConfig();
      sprintf(small_buffer, "new ssid:%s:%s., restarting", wifi_ssid, wifi_pass);
      webResponse(200, "OK", small_buffer);
      sprintf(large_buffer, "Restarting to switch to '%s' network.", wifi_ssid);
      pixels.clear();
      system_scroll(12, large_buffer, RGB(0,0,255));
      ESP.restart();
    } else {
      webResponse(400, "ERROR", "ssid and pass required");
    }
  });

  server.begin();
}

void handleUdp()
{
  int udpSize = Udp.parsePacket();
  if (udpSize) {
    int n = Udp.read(udp_buffer, UDP_TX_PACKET_MAX_SIZE);
    if (n > 5) 
    {
      if (udp_buffer[0] == 'R' && udp_buffer[1] == 'G' && udp_buffer[2] == 'B')
      {
        last_activity = millis();
        stopAnimation();
        int sy = udp_buffer[3]*256 + udp_buffer[4];
        int index = 5;
        int linesCount = (udpSize - 5) / (WIDTH*3);
        for(int y=sy;y<sy+linesCount;y++)
        {
          for(int x=0; x<WIDTH; x++)
          {
            set_pixel(x, y, udp_buffer[index], udp_buffer[index+1], udp_buffer[index+2]);
            index = index + 3;
          }
        }

        if (sy+linesCount == HEIGHT)
          pixels.show();
      }
    }
  }
}

void handleButton()
{
  button_prev = button_now;
  button_now = digitalRead(BUTTON_PIN);
  if (button_prev == LOW && button_now == HIGH) {
    oledText(56,55, "   ");
    int nr = (animation + 1) % (ANIMATIONS_COUNT+1);
    startAnimation(nr);
  } else if (button_prev == HIGH && button_now == LOW) {
    oledText(56,55, "BTN");
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  LittleFS.begin();
  readMemory();
    
  Wire.begin(14, 12);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED SSD1306 Initializarion error");
  } else {
    Serial.println("OLED SSD1306 OK");
  }

  display.clearDisplay(); 
  display.display();
  oledText(0,0,"start");

  oledText(0,8,"pixels...");
  init_pixel_indexes();
  pixels.begin();
  pixels.setBrightness(memory.brightness);
  pixels.clear();
  pixels.show(); 
  delay(300);
  oledText(116,8,"OK");

  if (digitalRead(BUTTON_PIN) == LOW)
      checkFactoryReset();

  setupWifi();

  oledText(0,24,"OTA...");
  setupOTA();
  delay(300);
  oledText(116,24,"OK");

  oledText(0,32,"webserver...");
  setupWebServer();
  delay(300);
  oledText(116,32,"OK");

  digitalWrite(LED_BUILTIN, HIGH);
  oledText(0,40,"Setup done.");
  delay(500);
  display.clearDisplay();
  oledText(0,0,"IP:");
  oledText(50,0,wifi_ap ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str());
  oledText(0,8,"waiting for commands ");
  if (wifi_ap)
  {
    animation = ANIMATION_NONE;
    pixels.clear();
    draw_text(4, 8, 0, "conf", RGB(0,0,255));
    draw_text(4, 16, 0, "mode", RGB(0,0,255));
    pixels.show();
  }
  else
  {
      startAnimation(memory.animation);
  }
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  handleUdp();
  handleButton();
  if (!wifi_ap)
    update_clock();
  
  delay(2);
  counter++;

  if (animation != ANIMATION_NONE) {
    animate();
  }

  if (counter%30 == 0) {
      oledText(122, 56, counter % 60 < 30 ? " " : ".");
  }

  if (counter%5000) {
    sprintf(small_buffer, "wifi: %d dBm  ", WiFi.RSSI());
    oledText(0, 8, small_buffer);
    int wifi_percent = 2 * (WiFi.RSSI() + 100);
    if (wifi_percent<0)
      wifi_percent = 0;
    if (wifi_percent>100)
      wifi_percent = 100;
    sprintf(small_buffer, "     %d%%", wifi_percent);
    oledText(127-strlen(small_buffer)*6, 8, small_buffer);
  }

  if (counter%50 == 0) {
    if (animation == ANIMATION_NONE) {
      int secs = (millis() - last_activity) / 1000;
      if (secs > AUTO_ANIMATION_SECS) {
        pixels.clear();
        pixels.show();
      }
    }
    else 
      oledText(0,56,"  ");
  } 

}
