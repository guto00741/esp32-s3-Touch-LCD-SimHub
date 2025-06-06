#include <TFT_eSPI.h>
#include <SPI.h>

// --- Configurações FreeRTOS ---
TaskHandle_t serialTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
SemaphoreHandle_t xDataMutex;

// —————————————— CONFIGURAÇÃO DE PINS E PARÂMETROS ——————————————
static const uint16_t SCREEN_W = 240;
static const uint16_t SCREEN_H = 240;
static const int16_t ACTUAL_CENTER_X = SCREEN_W / 2;
static const int16_t ACTUAL_CENTER_Y = SCREEN_H / 2;

// Texto de RPM
static const int16_t RPM_TEXT_Y_POS = ACTUAL_CENTER_Y - 95;
// Velocidade
static const int16_t SPEED_Y_POS = ACTUAL_CENTER_Y - 50;
// Barra de RPM Horizontal
static const int16_t RPM_BAR_X = 0;
static const int16_t RPM_BAR_Y = ACTUAL_CENTER_Y - 15;
static const int16_t RPM_BAR_W = SCREEN_W;
static const int16_t RPM_BAR_H = 30;
// Marcha
static const int16_t GEAR_Y_POS = ACTUAL_CENTER_Y + 45;


// --- Limites de RPM para Cores da Barra e Texto ---
volatile int vRpmMax          = 8000;
volatile int vRpmYellowStart  = 4000;
volatile int vRpmRedStart     = 6000;

const uint16_t COLOR_RPM_GREEN  = TFT_GREEN;
const uint16_t COLOR_RPM_YELLOW = TFT_YELLOW;
const uint16_t COLOR_RPM_RED    = TFT_RED;
const uint16_t RPM_BAR_INACTIVE_COLOR = TFT_DARKGREY;

static const int16_t INDICATOR_TEXT_Y_POS_OFFSET = 75;
static const int16_t INDICATOR_X_OFFSET   = 55;
static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t FRAME_DELAY_MS = 1000 / 60; // Target 60 FPS

// --- Fontes Conforme SEU LAYOUT ---
extern const GFXfont FreeSansBold12pt7b;
extern const GFXfont FreeSansBold24pt7b;
extern const GFXfont FreeSans24pt7b;

TFT_eSPI tft = TFT_eSPI();

volatile int currentRPM   = 0;
volatile int currentSpeed = 0;
volatile int currentGear  = 0;
volatile int absState     = -1;
volatile int tcState      = -1;

// --- Variáveis para Timeout de Dados ---
volatile unsigned long lastDataReceiveTime;
const unsigned long DATA_TIMEOUT_MS = 3000; // 3 segundos

int prevRPM   = -1;
int prevSpeed = -1;
int prevGear  = -1;
int prevAbsState = -2;
int prevTcState  = -2;
int prev_vRpmMax = -1;
int prev_vRpmYellowStart = -1;
int prev_vRpmRedStart = -1;

uint16_t currentBgColor = TFT_BLACK;
bool backgroundChangedThisFrame = false;
bool blinkState = false;
uint32_t lastBlinkTime = 0;
const uint32_t BLINK_INTERVAL_MS = 250;

static const int MAX_BUF = 100;
char lineBuf[MAX_BUF];
int  linePos = 0;

// —————————————— Funções Auxiliares de Display ——————————————

void updateRpmDerivedParametersDisplay() {
    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    if (vRpmMax <= 0) vRpmMax = 1;
    xSemaphoreGive(xDataMutex);
    Serial.println("RPM Parameters Updated (Display Task).");
    backgroundChangedThisFrame = true;
    prevRPM = -1;
}

void updateAndDrawBackgroundDisplay() {
    if (backgroundChangedThisFrame) {
        tft.fillScreen(currentBgColor);
    }
}

uint16_t interpolateColor(uint16_t color1, uint16_t color2, float factor) {
    if (factor <= 0.0) return color1;
    if (factor >= 1.0) return color2;
    uint8_t r1 = (color1 >> 11) & 0x1F; uint8_t g1 = (color1 >> 5) & 0x3F; uint8_t b1 = color1 & 0x1F;
    uint8_t r2 = (color2 >> 11) & 0x1F; uint8_t g2 = (color2 >> 5) & 0x3F; uint8_t b2 = color2 & 0x1F;
    uint8_t r = r1 + (uint8_t)((float)(r2 - r1) * factor);
    uint8_t g = g1 + (uint8_t)((float)(g2 - g1) * factor);
    uint8_t b = b1 + (uint8_t)((float)(b2 - b1) * factor);
    return (r << 11) | (g << 5) | b;
}

void drawHorizontalRpmBar(int rpm_val, int prevRpmVal) {
    int local_vRpmMax, local_vRpmYellowStart, local_vRpmRedStart;
    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    local_vRpmMax = vRpmMax;
    local_vRpmYellowStart = vRpmYellowStart;
    local_vRpmRedStart = vRpmRedStart;
    xSemaphoreGive(xDataMutex);

    if (local_vRpmMax <= 0) return;

    bool currently_at_max = (rpm_val >= local_vRpmMax);
    bool previously_at_max = (prevRpmVal >= local_vRpmMax && prevRpmVal != -1);

    if (currently_at_max) {
        if (!previously_at_max || backgroundChangedThisFrame || rpm_val != prevRpmVal) {
            tft.fillRect(RPM_BAR_X, RPM_BAR_Y, RPM_BAR_W, RPM_BAR_H, COLOR_RPM_RED);
        }
        return;
    }

    if (rpm_val == prevRpmVal && !backgroundChangedThisFrame && !previously_at_max) {
        return;
    }

    int filled_width = map(rpm_val, 0, local_vRpmMax, 0, RPM_BAR_W);
    filled_width = constrain(filled_width, 0, RPM_BAR_W);
    int prev_filled_width_for_drawing;

    if (backgroundChangedThisFrame || (previously_at_max && !currently_at_max)) {
        tft.fillRect(RPM_BAR_X, RPM_BAR_Y, RPM_BAR_W, RPM_BAR_H, RPM_BAR_INACTIVE_COLOR);
        prev_filled_width_for_drawing = 0;
    } else if (prevRpmVal < 0) {
        tft.fillRect(RPM_BAR_X, RPM_BAR_Y, RPM_BAR_W, RPM_BAR_H, RPM_BAR_INACTIVE_COLOR);
        prev_filled_width_for_drawing = 0;
    } else {
        prev_filled_width_for_drawing = map(prevRpmVal, 0, local_vRpmMax, 0, RPM_BAR_W);
        prev_filled_width_for_drawing = constrain(prev_filled_width_for_drawing, 0, RPM_BAR_W);
    }

    if (filled_width > prev_filled_width_for_drawing) {
        for (int x = prev_filled_width_for_drawing; x < filled_width; ++x) {
            float current_rpm_at_x = map(x, 0, RPM_BAR_W - 1, 0, local_vRpmMax);
            uint16_t seg_color;
            if (current_rpm_at_x >= local_vRpmRedStart) {
                seg_color = COLOR_RPM_RED;
            } else if (current_rpm_at_x >= local_vRpmYellowStart) {
                float factor = (local_vRpmRedStart - local_vRpmYellowStart == 0) ? 1.0f :
                               (float)(current_rpm_at_x - local_vRpmYellowStart) / (local_vRpmRedStart - local_vRpmYellowStart);
                seg_color = interpolateColor(COLOR_RPM_YELLOW, COLOR_RPM_RED, factor);
            } else {
                float factor = (local_vRpmYellowStart == 0) ? 1.0f :
                               (float)current_rpm_at_x / local_vRpmYellowStart;
                seg_color = interpolateColor(COLOR_RPM_GREEN, COLOR_RPM_YELLOW, factor);
            }
            tft.drawFastVLine(RPM_BAR_X + x, RPM_BAR_Y, RPM_BAR_H, seg_color);
        }
    } else if (filled_width < prev_filled_width_for_drawing) {
        tft.fillRect(RPM_BAR_X + filled_width, RPM_BAR_Y, prev_filled_width_for_drawing - filled_width, RPM_BAR_H, RPM_BAR_INACTIVE_COLOR);
    }
}

void drawRpmTextDisplay(int rpm_val, int prevRpmVal) {
    if (rpm_val == prevRpmVal && !backgroundChangedThisFrame && prevRpmVal != -1) return;
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    char buf[10]; snprintf(buf, sizeof(buf), "%d", rpm_val);
    int16_t text_w = 80;
    int16_t text_h = 25;
    tft.fillRect(ACTUAL_CENTER_X - text_w/2, RPM_TEXT_Y_POS - text_h/2, text_w, text_h, currentBgColor);
    tft.setTextColor(TFT_WHITE, currentBgColor);
    tft.drawString(buf, ACTUAL_CENTER_X, RPM_TEXT_Y_POS);
}

void drawSpeedDisplay(int speed_val, int prevSpeedVal) {
  if (speed_val == prevSpeedVal && !backgroundChangedThisFrame) return;
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold24pt7b);
  char buf[16]; snprintf(buf, sizeof(buf), "%d", speed_val);
  int16_t text_w = 140;
  int16_t text_h = 40;
  tft.fillRect(ACTUAL_CENTER_X - text_w/2, SPEED_Y_POS - text_h/2, text_w, text_h, currentBgColor);
  tft.setTextColor(TFT_WHITE, currentBgColor);
  tft.drawString(buf, ACTUAL_CENTER_X, SPEED_Y_POS);
}

void drawGearDisplay(int gear_val, int prevGearVal) {
  if (gear_val == prevGearVal && !backgroundChangedThisFrame) return;
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSans24pt7b);
  char buf[8];
  if (gear_val == 0) snprintf(buf, sizeof(buf), "N");
  else if (gear_val == -1) snprintf(buf, sizeof(buf), "R");
  else if (gear_val > 0 && gear_val < 10) snprintf(buf, sizeof(buf), "%d", gear_val);
  else snprintf(buf, sizeof(buf), "-");
  int16_t text_w = 90;
  int16_t text_h = 40;
  tft.fillRect(ACTUAL_CENTER_X - text_w/2, GEAR_Y_POS - text_h/2, text_w, text_h, currentBgColor);
  tft.setTextColor(TFT_CYAN, currentBgColor);
  tft.drawString(buf, ACTUAL_CENTER_X, GEAR_Y_POS);
}

void drawIndicatorDisplay(const char* label, int state_val, int prevState_val, bool blinkStatusChangedWhileTriggered) {
    bool valueChanged = (state_val != prevState_val);
    if (!valueChanged && !blinkStatusChangedWhileTriggered && !backgroundChangedThisFrame) return;
    uint16_t color = TFT_DARKGREY;
    if (state_val == 0) color = TFT_WHITE;
    else if (state_val == 1) color = blinkState ? TFT_RED : currentBgColor;
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    int16_t text_w = 40; int16_t text_h = 15;
    int16_t x_pos = (label[0] == 'T') ? (ACTUAL_CENTER_X - INDICATOR_X_OFFSET) : (ACTUAL_CENTER_X + INDICATOR_X_OFFSET);
    int16_t y_pos = ACTUAL_CENTER_Y + INDICATOR_TEXT_Y_POS_OFFSET;
    tft.fillRect(x_pos - text_w / 2, y_pos - text_h / 2, text_w, text_h, currentBgColor);
    tft.setTextColor(color, currentBgColor);
    tft.drawString(label, x_pos, y_pos);
}

// —————————————— Tarefa de Leitura Serial (Core 0) ——————————————
void serialTask(void *pvParameters) {
    Serial.println("serialTask running on Core 0");
    char localLineBuf[MAX_BUF];
    int localLinePos = 0;
    for (;;) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\r') continue;
            if (c == '\n') {
                localLineBuf[localLinePos] = '\0';
                xSemaphoreTake(xDataMutex, portMAX_DELAY);
                if (strncmp(localLineBuf, "SETUP:", 6) == 0) {
                    int max_r, yellow_s, red_s;
                    if (sscanf(localLineBuf + 6, "%d,%d,%d", &max_r, &yellow_s, &red_s) == 3) {
                        vRpmMax = max_r;
                        vRpmYellowStart = yellow_s;
                        vRpmRedStart = red_s;
                        Serial.printf("SETUP received: Max=%d, Yellow=%d, Red=%d\n", vRpmMax, vRpmYellowStart, vRpmRedStart);
                        lastDataReceiveTime = millis(); // Reset timeout on SETUP too, as it's a form of communication
                    } else { Serial.println("Erro parse SETUP (serialTask)"); }
                } else if (strncmp(localLineBuf, "DATA:", 5) == 0) {
                    int r, s, g, abs_s, tc_s;
                    if (sscanf(localLineBuf + 5, "%d,%d,%d,%d,%d", &r, &s, &g, &abs_s, &tc_s) == 5) {
                        currentRPM = constrain(r,0, vRpmMax + 2000);
                        currentSpeed = constrain(s,0,999);
                        currentGear = constrain(g,-1,9);
                        absState = constrain(abs_s,-1,1);
                        tcState = constrain(tc_s,-1,1);
                        lastDataReceiveTime = millis(); // <<<< ATUALIZA O TIMESTAMP DO ÚLTIMO DADO VÁLIDO
                    } else { Serial.println("Erro parse DATA (serialTask)"); }
                }
                xSemaphoreGive(xDataMutex);
                localLinePos = 0;
            } else {
                if (localLinePos < MAX_BUF - 1) localLineBuf[localLinePos++] = c; else localLinePos = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// —————————————— Tarefa de Display (Core 1) ——————————————
void displayTask(void *pvParameters) {
    Serial.println("displayTask running on Core 1");
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(false);

    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    prev_vRpmMax = vRpmMax;
    prev_vRpmYellowStart = vRpmYellowStart;
    prev_vRpmRedStart = vRpmRedStart;
    xSemaphoreGive(xDataMutex);

    updateRpmDerivedParametersDisplay();

    currentBgColor = TFT_BLACK;
    tft.fillScreen(currentBgColor);
    backgroundChangedThisFrame = true;
    prevRPM = -1;

    for (;;) {
        uint32_t frameStart = millis();
        bool actualBlinkStateChanged = false;
        int local_currentRPM, local_currentSpeed, local_currentGear, local_absState, local_tcState;
        int local_vRpmMax_from_serial, local_vRpmYellowStart_from_serial, local_vRpmRedStart_from_serial;
        unsigned long current_time_millis = millis(); // Get current time once for this frame

        if (current_time_millis - lastBlinkTime > BLINK_INTERVAL_MS) {
            bool oldHardwareBlinkState = blinkState; blinkState = !blinkState;
            lastBlinkTime = current_time_millis; if (oldHardwareBlinkState != blinkState) actualBlinkStateChanged = true;
        }

        xSemaphoreTake(xDataMutex, portMAX_DELAY);

        // --- VERIFICAÇÃO DE TIMEOUT DE DADOS ---
        if ((current_time_millis - lastDataReceiveTime) > DATA_TIMEOUT_MS) {
            currentRPM = 0;
            currentSpeed = 0;
            currentGear = 0;    // Assume 0 como Neutro
            absState = -1;      // Valor padrão "desconhecido" ou "não ativo"
            tcState = -1;       // Valor padrão
            // Serial.println("DEBUG: Data timeout, resetting values."); // Uncomment for debugging
            // Não resete lastDataReceiveTime aqui, senão o timeout será contínuo
            // até que novos dados cheguem e atualizem lastDataReceiveTime na serialTask.
        }

        // Copia os valores (agora possivelmente zerados devido ao timeout) para variáveis locais
        local_currentRPM = currentRPM;
        local_currentSpeed = currentSpeed;
        local_currentGear = currentGear;
        local_absState = absState;
        local_tcState = tcState;
        local_vRpmMax_from_serial = vRpmMax;
        local_vRpmYellowStart_from_serial = vRpmYellowStart;
        local_vRpmRedStart_from_serial = vRpmRedStart;
        xSemaphoreGive(xDataMutex);

        if (local_vRpmMax_from_serial != prev_vRpmMax ||
            local_vRpmYellowStart_from_serial != prev_vRpmYellowStart ||
            local_vRpmRedStart_from_serial != prev_vRpmRedStart) {
            prev_vRpmMax = local_vRpmMax_from_serial;
            prev_vRpmYellowStart = local_vRpmYellowStart_from_serial;
            prev_vRpmRedStart = local_vRpmRedStart_from_serial;
            updateRpmDerivedParametersDisplay();
        }

        updateAndDrawBackgroundDisplay();

        drawRpmTextDisplay(local_currentRPM, prevRPM);
        drawSpeedDisplay(local_currentSpeed, prevSpeed);
        drawHorizontalRpmBar(local_currentRPM, prevRPM);
        drawGearDisplay(local_currentGear, prevGear);
        drawIndicatorDisplay("TC", local_tcState, prevTcState, actualBlinkStateChanged && local_tcState == 1);
        drawIndicatorDisplay("ABS", local_absState, prevAbsState, actualBlinkStateChanged && local_absState == 1);

        prevRPM = local_currentRPM;
        prevSpeed = local_currentSpeed; prevGear = local_currentGear;
        prevAbsState = local_absState; prevTcState = local_tcState;
        backgroundChangedThisFrame = false;

        uint32_t frameTime = millis() - frameStart;
        if (frameTime < FRAME_DELAY_MS) vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS - frameTime));
        else vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// —————————————— ARDUINO setup() & loop() Principal ——————————————
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println("ESP32 Dashboard - Timeout & Solid Red RPM Bar - Setup");

  lastDataReceiveTime = millis(); // Inicializa para evitar timeout imediato

  xDataMutex = xSemaphoreCreateMutex();
  if (xDataMutex == NULL) { Serial.println("Erro Mutex!"); while(1); }

  xTaskCreatePinnedToCore(displayTask, "DisplayTask", 12000, NULL, 2, &displayTaskHandle, APP_CPU_NUM);
  xTaskCreatePinnedToCore(serialTask, "SerialTask", 4000, NULL, 1, &serialTaskHandle, PRO_CPU_NUM);

  Serial.println("Tarefas (Display, Serial) criadas.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}