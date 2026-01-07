 * Description:
 *   这是一个基于 ESP32-S3 的智能鲸鱼小车项目，集成了：
 *   1. 麦克风阵列录音 (I2S, INMP441)
 *   2. 语音活动检测 (VAD)
 *   3. 百度语音识别 (STT)
 *   4. Coze (扣子) 大模型对话 (LLM)
 *   5. 百度语音合成 (TTS)
 *   6. 麦克纳姆轮全向运动控制 (TB6612)
 *   7. 串口指令控制 (状态机)
 * 
 * Hardware Pinout (硬件接线速查表):
 * --------------------------------------------------------------------------------------
 * 1. 外部串口 (连接 蓝牙/语音模块/上位机):
 *    TX -> ESP32 GPIO 18 (RX)
 *    RX -> ESP32 GPIO 17 (TX)
 *    GND -> GND (共地)
 * 
 * 2. 麦克风 (INMP441 - I2S0): 
 *    SCK -> GPIO 4
 *    WS  -> GPIO 5
 *    SD  -> GPIO 6
 * 
 * 3. 功放 (MAX98357A - I2S1): 
 *    BCLK -> GPIO 8
 *    LRC  -> GPIO 16
 *    DIN  -> GPIO 7
 * 
 * 4. 电机驱动 (TB6612 x2): 
 *    左前(FL): IN:42/41, PWM:40 
 *    右前(FR): IN:39/38, PWM:48 (注意: 避开 PSRAM 引脚 35-37)
 *    左后(RL): IN:10/11, PWM:12 
 *    右后(RR): IN:13/14, PWM:21
 * ======================================================================================
 */

#include <string.h>
#include <driver/i2s.h>
#include <WiFi.h>  
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <UrlEncode.h>

// =============================================================
//               PART 1: 用户配置区域 (User Configuration)
// =============================================================

// [WiFi 设置]
// TODO: 请修改为您的 WiFi 名称和密码
const char* ssid     = "YOUR_WIFI_SSID";    
const char* password = "YOUR_WIFI_PASSWORD"; 

// [百度智能云 API 配置]
// 用于语音识别(STT)和语音合成(TTS)
// 获取地址: https://console.bce.baidu.com/ai/
// TODO: 请填入您的百度 API Key 和 Secret Key
const char* baidu_api_key    = "YOUR_BAIDU_API_KEY";       
const char* baidu_secret_key = "YOUR_BAIDU_SECRET_KEY"; 
String baidu_access_token = ""; // 自动获取，无需手动填写

// [Coze (扣子) API 配置]
// 用于大模型对话逻辑
// 获取地址: https://www.coze.cn/
// TODO: 请填入您的 Coze PAT Token 和 Bot ID
const String coze_apiKey = "pat_YOUR_COZE_PAT_TOKEN"; 
const String botId       = "YOUR_COZE_BOT_ID";        

// [串口设置]
// 与外部模块通信的波特率
#define EXT_SERIAL_BAUD 115200 


// =============================================================
//               PART 2: 硬件引脚定义 (Hardware Pins)
// =============================================================

// --- 外部串口 ---
#define EXT_SERIAL_RX 18  
#define EXT_SERIAL_TX 17  

// --- 电机驱动 (TB6612) ---
#define PIN_FL_IN1 42 
#define PIN_FL_IN2 41 
#define PIN_FL_PWM 40 

#define PIN_FR_IN1 39
#define PIN_FR_IN2 38 
#define PIN_FR_PWM 48 // 注意: 使用 48 避开 OPI PSRAM 占用的 GPIO 35-37

#define PIN_RL_IN1 10
#define PIN_RL_IN2 11
#define PIN_RL_PWM 12

#define PIN_RR_IN1 13
#define PIN_RR_IN2 14
#define PIN_RR_PWM 21

// --- 麦克风 (I2S Input) ---
#define INMP441_WS 5
#define INMP441_SCK 4
#define INMP441_SD 6       

// --- 功放 (I2S Output) ---
#define MAX98357_LRC 16
#define MAX98357_BCLK 8 
#define MAX98357_DIN 7


// =============================================================
//               PART 3: 全局变量与系统参数
// =============================================================

HardwareSerial MySerial(1); // 定义硬件串口对象

// 系统运行状态机
enum SystemMode { 
  MODE_IDLE,    // 待机状态
  MODE_VOICE,   // 语音对话模式
  MODE_MOTION   // 运动控制模式
};
SystemMode currentMode = MODE_IDLE; 

// 语音模式控制标记
bool voiceTrigger = false; 

// 运动模式控制变量
bool isContinuous = false;      // false=点动模式(2s), true=持续模式
bool isMoving = false;          // 当前运动状态标记
unsigned long lastMoveTime = 0; // 上次运动指令的时间戳

// 音频参数
#define SAMPLE_RATE 16000
#define MAX_RECORD_SECONDS 15   // 最大录音时长(秒)
#define BUFFER_SIZE (SAMPLE_RATE * MAX_RECORD_SECONDS * 2) 

// VAD (语音活动检测) 参数
#define VAD_THRESHOLD 1000      // 音量阈值 (根据麦克风灵敏度调整)
#define VAD_SILENCE_MS 1000     // 静音超时判定 (ms)

// --- I2S 配置 (麦克风) ---
i2s_config_t i2sIn_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 1024
};

const i2s_pin_config_t i2sIn_pin_config = {
  .bck_io_num = INMP441_SCK,
  .ws_io_num = INMP441_WS,
  .data_out_num = -1,
  .data_in_num = INMP441_SD
};

// --- I2S 配置 (功放) ---
i2s_config_t i2sOut_config = {
  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = i2s_bits_per_sample_t(16),
  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 1024
};

const i2s_pin_config_t i2sOut_pin_config = {
  .bck_io_num = MAX98357_BCLK,
  .ws_io_num = MAX98357_LRC,
  .data_out_num = MAX98357_DIN,
  .data_in_num = -1
};


// =============================================================
//               PART 4: 函数声明 (Function Declarations)
// =============================================================
void setupMotors();
void stopMotors();
void controlMecanum(int x, int y, int w);
void setMotorSpeed(int pinIn1, int pinIn2, int pinPwm, int speed);
void handleSerialCommand();
void runVoiceMode();
void runMotionMode();

String getAccessToken(const char* api_key, const char* secret_key);
String ask_coze(const String& question);
String baiduSTT_Send(String access_token, uint8_t* audioData, int audioDataSize);
void baiduTTS_Send(String access_token, String text);
void playAudio(uint8_t* audioData, size_t audioDataSize);
void clearAudio(void);
void custom_base64_encode(char* output, uint8_t* input, int inputLen); 
void playSilence(); 


// =============================================================
//               PART 5: 主程序初始化 (Setup)
// =============================================================
void setup() {
  Serial.begin(115200);
  
  // 初始化外部控制串口
  MySerial.begin(EXT_SERIAL_BAUD, SERIAL_8N1, EXT_SERIAL_RX, EXT_SERIAL_TX);
  Serial.println("System Start");

  // 初始化电机
  setupMotors(); 

  // 连接 WiFi
  WiFi.begin(ssid, password);
  Serial.print("WiFi Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  // 初始化 I2S 驱动
  i2s_driver_install(I2S_NUM_0, &i2sIn_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &i2sIn_pin_config);
  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);

  // 获取百度 Token
  baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  Serial.println("Token: " + baidu_access_token);
  
  Serial.println("等待指令 (发送: '打开对话模式', '开始聊天', '打开运动模式', '停止')...");
}


// =============================================================
//               PART 6: 主循环 (Main Loop)
// =============================================================
void loop() {
  // 1. 处理串口指令 (最高优先级)
  handleSerialCommand();

  // 2. 根据状态机执行对应逻辑
  switch (currentMode) {
    case MODE_IDLE:
      delay(50);
      break;
    
    case MODE_VOICE:
      runVoiceMode(); 
      break;
      
    case MODE_MOTION:
      runMotionMode(); 
      break;
  }
}


// =============================================================
//               PART 7: 核心功能模块 (Core Modules)
// =============================================================

// --- [模块A] 串口指令处理 ---
void handleSerialCommand() {
  if (MySerial.available()) {
    String cmd = MySerial.readStringUntil('\n');
    cmd.trim(); 
    if (cmd.length() == 0) return;
    Serial.println("[CMD] " + cmd);

    // 1. 全局停止指令 (最高优先级)
    if (cmd == "stop" || cmd == "S") {
      stopMotors();
      currentMode = MODE_IDLE; 
      voiceTrigger = false;
      isMoving = false;    
      Serial.println(">>> [已停止] 回到待机");
      return; 
    }

    // 2. 模式切换指令
    if (cmd == "chatmode") {
      stopMotors();
      currentMode = MODE_VOICE;
      voiceTrigger = false;
      Serial.println(">>> [对话模式] 就绪");
      return;
    } 
    else if (cmd == "sportmode") {
      stopMotors(); 
      currentMode = MODE_MOTION;
      voiceTrigger = false;
      isContinuous = false; // 默认点动
      Serial.println(">>> [运动模式] 就绪 (默认点动)");
      return;
    }

    // 3. 待机状态屏蔽
    if (currentMode == MODE_IDLE) {
        Serial.println("!!! 待机中，请先选择模式");
        return;
    }

    // 4. 对话模式指令
    if (currentMode == MODE_VOICE) {
        if (cmd == "startchat") {
            voiceTrigger = true; 
            Serial.println(">>> 触发录音!");
        }
    } 
    // 5. 运动模式指令
    else if (currentMode == MODE_MOTION) {
        // 子模式切换
        if (cmd == "continuoussportmode") {
            isContinuous = true;
            Serial.println(">>> 已切换: 持续运动");
            return;
        }
        if (cmd == "pointsportmode") {
            isContinuous = false;
            stopMotors();
            Serial.println(">>> 已切换: 点动模式");
            return;
        }

        // 运动控制
        int speed = 180;
        bool validMove = false;

        if (cmd == "W" || cmd == "w") { controlMecanum(0, speed, 0); validMove = true; }
        else if (cmd == "S" || cmd == "s") { controlMecanum(0, -speed, 0); validMove = true; }
        else if (cmd == "A" || cmd == "a") { controlMecanum(-speed, 0, 0); validMove = true; }
        else if (cmd == "D" || cmd == "d") { controlMecanum(speed, 0, 0); validMove = true; }
        else if (cmd == "Q" || cmd == "q") { controlMecanum(0, 0, speed/2); validMove = true; }
        else if (cmd == "E" || cmd == "e") { controlMecanum(0, 0, -speed/2); validMove = true; }
        else if (cmd == "Z" || cmd == "z") { stopMotors(); isMoving = false; }

        if (validMove) {
            isMoving = true;
            lastMoveTime = millis();
        }
    }
  }
}

// --- [模块B] 运动模式循环检测 ---
void runMotionMode() {
    // 点动模式超时自动停止
    if (!isContinuous && isMoving) {
        if (millis() - lastMoveTime >= 2000) { // 2秒超时
            stopMotors();
            isMoving = false;
            Serial.println(">>> [自动停止] 点动时间到");
        }
    }
    delay(20);
}

// --- [模块C] 语音交互逻辑 (VAD) ---
void runVoiceMode() {
  // 1. 等待触发信号
  if (voiceTrigger == false) {
      return; 
  }
  voiceTrigger = false; 

  // 检查 Token
  if (baidu_access_token == "") {
     Serial.println("Token 无效，重试...");
     baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
     if(baidu_access_token == "") { delay(1000); return; }
  }

  // 申请录音内存
  uint8_t* pcm_data = (uint8_t*)ps_malloc(BUFFER_SIZE);
  if (pcm_data == NULL) {
    Serial.println("PSRAM Error"); 
    delay(1000); 
    return;
  }

  Serial.println(">>> 正在聆听..."); 
  clearAudio();
  
  size_t bytes_read = 0;
  size_t recordingSize = 0;
  int16_t data[512];
  
  unsigned long lastVoiceTime = millis(); 
  bool hasSpoken = false;                 

  // 2. VAD 录音循环
  while (true) {
    // [打断检测]
    if (MySerial.available()) {
       Serial.println(">>> 录音被打断");
       free(pcm_data);
       return; 
    }

    i2s_read(I2S_NUM_0, data, sizeof(data), &bytes_read, portMAX_DELAY);
    
    // 计算音量 & 增益
    long sumVol = 0;
    for (int i = 0; i < bytes_read / 2; i++) {
      int32_t val = data[i] * 50; 
      if (val > 32767) data[i] = 32767;
      else if (val < -32768) data[i] = -32768;
      else data[i] = (int16_t)val;
      sumVol += abs(data[i]);
    }
    int avgVol = sumVol / (bytes_read / 2);

    // VAD 判定
    if (avgVol > VAD_THRESHOLD) {
        lastVoiceTime = millis();
        if (!hasSpoken) {
            Serial.println("检测到语音...");
            hasSpoken = true;
        }
    } 
    
    // 静音超时停止
    if (hasSpoken && (millis() - lastVoiceTime > VAD_SILENCE_MS)) {
        Serial.println("说话结束");
        break; 
    }
    
    // 初始超时停止
    if (!hasSpoken && (millis() - lastVoiceTime > 10000)) {
        Serial.println("超时无人说话");
        free(pcm_data);
        return; 
    }

    // 存入内存
    if (recordingSize + bytes_read <= BUFFER_SIZE) {
        memcpy(pcm_data + recordingSize, data, bytes_read);
        recordingSize += bytes_read;
    } else {
        Serial.println("录音已满");
        break;
    }
  }

  // 过滤短噪音
  if (recordingSize < 16000) { 
      Serial.println("录音太短，忽略");
      free(pcm_data);
      return;
  }

  // [打断检测]
  if (MySerial.available()) { free(pcm_data); return; }

  // 3. 识别 (STT)
  Serial.println(">>> 正在识别...");
  String askText = baiduSTT_Send(baidu_access_token, pcm_data, recordingSize);
  free(pcm_data); 
  clearAudio();

  // [打断检测]
  if (MySerial.available()) return;

  if (askText.length() > 0) {
      Serial.println("Q: " + askText);
      
      // 4. 思考 (LLM)
      Serial.println(">>> 正在思考...");
      String recognizedText = ask_coze(askText);
      
      // [打断检测]
      if (MySerial.available()) return;

      Serial.println("A: " + recognizedText);
      
      // 5. 回复 (TTS)
      Serial.println(">>> 正在合成...");
      baiduTTS_Send(baidu_access_token, recognizedText);
  } else {
      Serial.println("未听到有效声音");
  }

  clearAudio();
}


// =============================================================
//               PART 8: API 实现 (API Implementation)
// =============================================================

// 1. 获取百度 Token
String getAccessToken(const char* api_key, const char* secret_key) {
  HTTPClient http;
  String url = "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=" + String(api_key) + "&client_secret=" + String(secret_key);
  http.begin(url);
  int httpCode = http.POST("");
  String access_token = "";
  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    access_token = doc["access_token"].as<String>();
  } 
  http.end();
  return access_token;
}

// 2. Coze 对话请求
String ask_coze(const String& question) {
  HTTPClient http;
  http.begin("https://api.coze.cn/open_api/v2/chat");
  http.addHeader("Authorization", "Bearer " + coze_apiKey);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "keep-alive");

  DynamicJsonDocument doc(1024);
  doc["bot_id"] = botId;
  doc["user"] = "esp32_user";
  doc["query"] = question;
  doc["stream"] = false;

  String payload;
  serializeJson(doc, payload);
  
  int code = http.POST(payload);
  String answer = "我不知道怎么回答";
  
  if(code == HTTP_CODE_OK) {
    String resp = http.getString();
    DynamicJsonDocument resDoc(4096);
    DeserializationError error = deserializeJson(resDoc, resp);
    
    if (!error) {
      if (resDoc.containsKey("messages")) {
        JsonArray msgs = resDoc["messages"];
        for (JsonObject msg : msgs) {
           if (msg["type"] == "answer") {
               answer = msg["content"].as<String>();
               break;
           }
        }
      }
    }
  }
  http.end();
  return answer;
}

// 3. 百度 STT (语音转文字)
String baiduSTT_Send(String access_token, uint8_t* audioData, int audioDataSize) {
  String recognizedText = "";
  if (access_token == "") return recognizedText;

  int encodedLen = (audioDataSize + 2) / 3 * 4;
  char* audioDataBase64 = (char*)ps_malloc(encodedLen + 1);
  if (!audioDataBase64) {
    Serial.println("STT RAM Error");
    return recognizedText;
  }

  custom_base64_encode(audioDataBase64, audioData, audioDataSize);
  
  String json = "{\"format\":\"pcm\",\"rate\":16000,\"dev_pid\":1537,\"channel\":1,\"cuid\":\"ESP32\",\"token\":\"" + access_token + "\",\"len\":" + String(audioDataSize) + ",\"speech\":\"" + String(audioDataBase64) + "\"}";

  HTTPClient http_client;
  http_client.begin("http://vop.baidu.com/server_api");
  http_client.addHeader("Content-Type", "application/json");
  int httpCode = http_client.POST(json);

  if (httpCode == HTTP_CODE_OK) {
      String response = http_client.getString();
      DynamicJsonDocument responseDoc(2048);
      deserializeJson(responseDoc, response);
      if (responseDoc.containsKey("result"))
         recognizedText = responseDoc["result"][0].as<String>();
  } else {
    Serial.printf("STT HTTP Error: %s\n", http_client.errorToString(httpCode).c_str());
  }

  free(audioDataBase64);
  http_client.end();
  clearAudio();
  return recognizedText;
}

// 4. 百度 TTS (文字转语音)
void baiduTTS_Send(String access_token, String text) {
  if (access_token == "" || text.length() == 0) return;

  Serial.println(">>> [TTS] 播放中...");
  String encodedText = urlEncode(text);
  String url = "https://tsn.baidu.com/text2audio";
  url += "?tok=" + access_token;
  url += "&tex=" + encodedText;
  url += "&per=103&spd=5&pit=5&vol=13&aue=4&cuid=esp32s3&lan=zh&ctp=1";

  const char* header[] = { "Content-Type", "Content-Length" };

  HTTPClient http;
  http.begin(url);
  http.collectHeaders(header, 2);

  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    if (httpResponseCode == HTTP_CODE_OK) {
      String contentType = http.header("Content-Type");
      if (contentType.startsWith("audio")) {
        Stream* stream = http.getStreamPtr();
        uint8_t buffer[1024]; 
        unsigned long lastDataTime = millis(); 
        
        while (http.connected()) {
          // [打断检测]
          if (MySerial.available()) {
              Serial.println("TTS被指令打断");
              break; 
          }
          
          size_t bytesRead = stream->readBytes(buffer, sizeof(buffer));
          if(bytesRead > 0) {
            playAudio(buffer, bytesRead);
            lastDataTime = millis(); 
          } else {
            // 超时判定 (300ms)
            if (millis() - lastDataTime >= 300) {
                Serial.println("播放超时");
                break;
            }
          }
        }
      } else {
        Serial.println("TTS Error: " + http.getString());
      }
    } else {
      Serial.println("Failed to receive audio file");
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpResponseCode);
  }
  http.end();
  clearAudio();
}


// =============================================================
//               PART 9: 底层驱动与工具 (Low-level Drivers)
// =============================================================

// I2S 播放
void playAudio(uint8_t* audioData, size_t audioDataSize) {
  size_t bytes_written = 0;
  i2s_write(I2S_NUM_1, (int16_t*)audioData, audioDataSize, &bytes_written, portMAX_DELAY);
}

// 播放静音片段
void playSilence() {
  uint8_t silence[1024];
  memset(silence, 0, 1024);
  playAudio(silence, 1024);
  playAudio(silence, 1024);
  playAudio(silence, 1024);
}

// 清空 I2S 缓存
void clearAudio(void) {
  i2s_zero_dma_buffer(I2S_NUM_1); 
  i2s_zero_dma_buffer(I2S_NUM_0); 
}

// 手写 Base64 编码 (无第三方库依赖)
const char b64_tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
void custom_base64_encode(char* out, uint8_t* in, int len) {
    int i=0, j=0, enc=0; unsigned char a3[3], a4[4];
    while (len--) {
        a3[i++] = *(in++);
        if (i == 3) {
            a4[0] = (a3[0]&0xfc)>>2; a4[1] = ((a3[0]&0x03)<<4)+((a3[1]&0xf0)>>4);
            a4[2] = ((a3[1]&0x0f)<<2)+((a3[2]&0xc0)>>6); a4[3] = a3[2]&0x3f;
            for(i=0;i<4;i++) out[enc++] = b64_tbl[a4[i]];
            i=0;
        }
    }
    if (i) {
        for(j=i;j<3;j++) a3[j]='\0';
        a4[0] = (a3[0]&0xfc)>>2; a4[1] = ((a3[0]&0x03)<<4)+((a3[1]&0xf0)>>4);
        a4[2] = ((a3[1]&0x0f)<<2)+((a3[2]&0xc0)>>6); a4[3] = a3[2]&0x3f;
        for(j=0;j<i+1;j++) out[enc++] = b64_tbl[a4[j]];
        while(i++<3) out[enc++]='=';
    }
    out[enc] = '\0';
}

// 电机引脚初始化
void setupMotors() {
  pinMode(PIN_FL_IN1, OUTPUT); pinMode(PIN_FL_IN2, OUTPUT); pinMode(PIN_FL_PWM, OUTPUT);
  pinMode(PIN_FR_IN1, OUTPUT); pinMode(PIN_FR_IN2, OUTPUT); pinMode(PIN_FR_PWM, OUTPUT);
  pinMode(PIN_RL_IN1, OUTPUT); pinMode(PIN_RL_IN2, OUTPUT); pinMode(PIN_RL_PWM, OUTPUT);
  pinMode(PIN_RR_IN1, OUTPUT); pinMode(PIN_RR_IN2, OUTPUT); pinMode(PIN_RR_PWM, OUTPUT);
  stopMotors();
}

// 电机停车
void stopMotors() { controlMecanum(0, 0, 0); }

// 麦克纳姆轮运动解算
void controlMecanum(int x, int y, int w) {
  setMotorSpeed(PIN_FL_IN1, PIN_FL_IN2, PIN_FL_PWM, y + x + w);
  setMotorSpeed(PIN_FR_IN1, PIN_FR_IN2, PIN_FR_PWM, y - x - w);
  setMotorSpeed(PIN_RL_IN1, PIN_RL_IN2, PIN_RL_PWM, y - x + w);
  setMotorSpeed(PIN_RR_IN1, PIN_RR_IN2, PIN_RR_PWM, y + x - w);
}

// 单个电机驱动
void setMotorSpeed(int p1, int p2, int pwm, int s) {
  if (s>255) s=255; if (s<-255) s=-255;
  if (s>0) { digitalWrite(p1, HIGH); digitalWrite(p2, LOW); analogWrite(pwm, s); }
  else if (s<0) { digitalWrite(p1, LOW); digitalWrite(p2, HIGH); analogWrite(pwm, -s); }
  else { digitalWrite(p1, LOW); digitalWrite(p2, LOW); analogWrite(pwm, 0); }
}