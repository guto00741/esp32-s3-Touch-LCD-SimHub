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

// --- Parâmetros da Barra de RPM em Arco (Topo, Esquerda para Direita) ---
static const int16_t ARC_RPM_CENTER_X = ACTUAL_CENTER_X; // Centro horizontal da tela
static const int16_t ARC_RPM_CENTER_Y = 120;             // Centro Y na base da tela para o arco ficar no topo
static const int16_t ARC_RPM_OUTER_RADIUS = 120;         // Raio externo
static const int16_t ARC_RPM_THICKNESS = 20;             // Espessura
static const int16_t ARC_RPM_INNER_RADIUS = ARC_RPM_OUTER_RADIUS - ARC_RPM_THICKNESS;

// Ângulos para o arco (0=direita, 90=baixo, 180=esquerda, 270=topo)
// O arco irá no topo, crescendo da esquerda (180°) para a direita (360°/0°).
// Usamos 360 para criar um intervalo contínuo e crescente para a função map().
static const int16_t ARC_RPM_TOTAL_START_ANGLE = 90; // 0 RPM está na esquerda
static const int16_t ARC_RPM_TOTAL_END_ANGLE = 270;  // vRpmMax está na direita

// --- Novas Posições Y dos Elementos ---
// Texto de RPM
static const int16_t RPM_TEXT_Y_POS = ACTUAL_CENTER_Y - 40; // Abaixo do arco
// Velocidade
static const int16_t SPEED_Y_POS = ACTUAL_CENTER_Y; // Mais para o centro
// Marcha
static const int16_t GEAR_Y_POS = SPEED_Y_POS + 55; // Abaixo da velocidade
// Indicadores TC/ABS
static const int16_t INDICATOR_TEXT_Y_POS = GEAR_Y_POS + 20; // Abaixo da marcha, mais para cima

// --- Limites de RPM para Cores da Barra ---
volatile int vRpmMax = 8000;
volatile int vRpmYellowStart = 4000;
volatile int vRpmRedStart = 6000;

const uint16_t COLOR_RPM_GREEN = TFT_GREEN;
const uint16_t COLOR_RPM_YELLOW = TFT_YELLOW;
const uint16_t COLOR_RPM_RED = TFT_RED;
const uint16_t RPM_BAR_INACTIVE_COLOR = TFT_DARKGREY;

static const int16_t INDICATOR_X_OFFSET = 55;
static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t FRAME_DELAY_MS = 1000 / 60; // Target 60 FPS

// --- Contador de FPS ---
#define SHOW_FPS_COUNTER false // Mude para false para desabilitar
#if SHOW_FPS_COUNTER
uint32_t frameCount = 0;
uint32_t lastFpsTime = 0;
float currentFPS = 0;
#endif

// --- Fontes Conforme SEU LAYOUT ---
extern const GFXfont FreeSansBold12pt7b;
extern const GFXfont FreeSansBold24pt7b;
extern const GFXfont FreeSans24pt7b;

TFT_eSPI tft = TFT_eSPI();

volatile int currentRPM = 0;
volatile int currentSpeed = 0;
volatile int currentGear = 0;
volatile int absState = -1;
volatile int tcState = -1;

volatile unsigned long lastDataReceiveTime;
const unsigned long DATA_TIMEOUT_MS = 10000;

int prevRPM = -1;
int prevSpeed = -1;
int prevGear = -1;
int prevAbsState = -2;
int prevTcState = -2;
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
int linePos = 0;

// —————————————— Funções Auxiliares de Display ——————————————

void updateRpmDerivedParametersDisplay()
{
    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    if (vRpmMax <= 0)
        vRpmMax = 1;
    xSemaphoreGive(xDataMutex);
    Serial.println("RPM Parameters Updated (Display Task).");
    backgroundChangedThisFrame = true;
    prevRPM = -1;
}

void updateAndDrawBackgroundDisplay()
{
    if (backgroundChangedThisFrame)
    {
        tft.fillScreen(currentBgColor);
    }
}

void drawArcRpmBar(int rpm_val, int prevRpmVal)
{
    int local_vRpmMax, local_vRpmYellowStart, local_vRpmRedStart;
    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    local_vRpmMax = vRpmMax;
    local_vRpmYellowStart = vRpmYellowStart;
    local_vRpmRedStart = vRpmRedStart;
    xSemaphoreGive(xDataMutex);

    if (local_vRpmMax <= 0)
        return;

    bool currently_at_max = (rpm_val >= local_vRpmMax);
    bool previously_at_max = (prevRpmVal >= local_vRpmMax && prevRpmVal != -1);

    if (rpm_val == prevRpmVal && !backgroundChangedThisFrame && (currently_at_max == previously_at_max))
    {
        return;
    }

    // Mapeia RPM para o ângulo final do arco.
    // Ângulos AUMENTAM com aumento de RPM (180 -> 360).
    int current_rpm_end_angle_deg = map(rpm_val, 0, local_vRpmMax, ARC_RPM_TOTAL_START_ANGLE, ARC_RPM_TOTAL_END_ANGLE);
    current_rpm_end_angle_deg = constrain(current_rpm_end_angle_deg, ARC_RPM_TOTAL_START_ANGLE, ARC_RPM_TOTAL_END_ANGLE);

    if (backgroundChangedThisFrame || (previously_at_max && !currently_at_max) || prevRpmVal == -1)
    {
        // Desenha a barra de fundo cinza inteira
        tft.drawSmoothArc(ARC_RPM_CENTER_X, ARC_RPM_CENTER_Y, ARC_RPM_OUTER_RADIUS, ARC_RPM_INNER_RADIUS,
                          ARC_RPM_TOTAL_START_ANGLE, ARC_RPM_TOTAL_END_ANGLE,
                          RPM_BAR_INACTIVE_COLOR, currentBgColor, false);
    }
    else
    {
        int prev_rpm_end_angle_deg = map(prevRpmVal, 0, local_vRpmMax, ARC_RPM_TOTAL_START_ANGLE, ARC_RPM_TOTAL_END_ANGLE);
        prev_rpm_end_angle_deg = constrain(prev_rpm_end_angle_deg, ARC_RPM_TOTAL_START_ANGLE, ARC_RPM_TOTAL_END_ANGLE);

        // Se RPM diminuiu, o ângulo final é menor.
        if (current_rpm_end_angle_deg < prev_rpm_end_angle_deg)
        {
            // Limpa do NOVO fim até o FIM TOTAL da barra
            tft.drawSmoothArc(ARC_RPM_CENTER_X, ARC_RPM_CENTER_Y, ARC_RPM_OUTER_RADIUS, ARC_RPM_INNER_RADIUS,
                              current_rpm_end_angle_deg, ARC_RPM_TOTAL_END_ANGLE,
                              RPM_BAR_INACTIVE_COLOR, currentBgColor, false);
        }
    }

    // Determina a cor baseada no RPM atual
    uint16_t active_color;
    if (rpm_val >= local_vRpmRedStart || currently_at_max)
    {
        active_color = COLOR_RPM_RED;
    }
    else if (rpm_val >= local_vRpmYellowStart)
    {
        active_color = COLOR_RPM_YELLOW;
    }
    else
    {
        active_color = COLOR_RPM_GREEN;
    }

    if (rpm_val <= 0)
    {
        if (prevRpmVal > 0 && !backgroundChangedThisFrame)
        {
            tft.drawSmoothArc(ARC_RPM_CENTER_X, ARC_RPM_CENTER_Y, ARC_RPM_OUTER_RADIUS, ARC_RPM_INNER_RADIUS,
                              ARC_RPM_TOTAL_START_ANGLE, ARC_RPM_TOTAL_END_ANGLE,
                              RPM_BAR_INACTIVE_COLOR, currentBgColor, false);
        }
        return;
    }

    // Desenha o arco ativo
    tft.drawSmoothArc(ARC_RPM_CENTER_X, ARC_RPM_CENTER_Y, ARC_RPM_OUTER_RADIUS, ARC_RPM_INNER_RADIUS,
                      ARC_RPM_TOTAL_START_ANGLE, current_rpm_end_angle_deg,
                      active_color, currentBgColor, false);
}

void drawRpmTextDisplay(int rpm_val, int prevRpmVal)
{
    if (rpm_val == prevRpmVal && !backgroundChangedThisFrame && prevRpmVal != -1)
        return;
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    char buf[10];
    snprintf(buf, sizeof(buf), "%d", rpm_val);
    int16_t text_w = 80;
    int16_t text_h = 25;
    tft.fillRect(ACTUAL_CENTER_X - text_w / 2, RPM_TEXT_Y_POS - text_h / 2, text_w, text_h, currentBgColor);
    tft.setTextColor(TFT_WHITE, currentBgColor);
    tft.drawString(buf, ACTUAL_CENTER_X, RPM_TEXT_Y_POS);
}

void drawSpeedDisplay(int speed_val, int prevSpeedVal)
{
    if (speed_val == prevSpeedVal && !backgroundChangedThisFrame)
        return;
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold24pt7b);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", speed_val);
    int16_t text_w = 140;
    int16_t text_h = 40;
    tft.fillRect(ACTUAL_CENTER_X - text_w / 2, SPEED_Y_POS - text_h / 2, text_w, text_h, currentBgColor);
    tft.setTextColor(TFT_WHITE, currentBgColor);
    tft.drawString(buf, ACTUAL_CENTER_X, SPEED_Y_POS);
}

void drawGearDisplay(int gear_val, int prevGearVal)
{
    if (gear_val == prevGearVal && !backgroundChangedThisFrame)
        return;
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSans24pt7b);
    char buf[8];
    if (gear_val == 0)
        snprintf(buf, sizeof(buf), "N");
    else if (gear_val == -1)
        snprintf(buf, sizeof(buf), "R");
    else if (gear_val > 0 && gear_val < 10)
        snprintf(buf, sizeof(buf), "%d", gear_val);
    else
        snprintf(buf, sizeof(buf), "-");
    int16_t text_w = 70;
    int16_t text_h = 40;
    tft.fillRect(ACTUAL_CENTER_X - text_w / 2, GEAR_Y_POS - text_h / 2, text_w, text_h, currentBgColor);
    tft.setTextColor(TFT_CYAN, currentBgColor);
    tft.drawString(buf, ACTUAL_CENTER_X, GEAR_Y_POS);
}

void drawIndicatorDisplay(const char *label, int state_val, int prevState_val, bool blinkStatusChangedWhileTriggered)
{
    bool valueChanged = (state_val != prevState_val);
    if (!valueChanged && !blinkStatusChangedWhileTriggered && !backgroundChangedThisFrame)
        return;
    uint16_t color = TFT_DARKGREY;
    if (state_val == 0)
        color = TFT_WHITE;
    else if (state_val == 1)
        color = blinkState ? TFT_RED : currentBgColor;

    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    int16_t text_w = 40;
    int16_t text_h = 15;
    int16_t x_pos = (label[0] == 'T') ? (ACTUAL_CENTER_X - INDICATOR_X_OFFSET) : (ACTUAL_CENTER_X + INDICATOR_X_OFFSET);
    int16_t y_pos = INDICATOR_TEXT_Y_POS;
    tft.fillRect(x_pos - text_w / 2, y_pos - text_h / 2, text_w, text_h, currentBgColor);
    tft.setTextColor(color, currentBgColor);
    tft.drawString(label, x_pos, y_pos);
}

#if SHOW_FPS_COUNTER
void drawFpsCounter()
{
    frameCount++;
    unsigned long currentTime = millis();
    if (currentTime - lastFpsTime >= 1000)
    {
        currentFPS = (float)frameCount * 1000.0f / (float)(currentTime - lastFpsTime);
        lastFpsTime = currentTime;
        frameCount = 0;
    }
    tft.setTextColor(TFT_YELLOW, currentBgColor);
    tft.setTextDatum(TR_DATUM); // Top Right
    char fpsBuf[10];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.1f", currentFPS);
    tft.fillRect(SCREEN_W - 40, 0, 40, 12, currentBgColor); // Limpa área do FPS
    tft.drawString(fpsBuf, SCREEN_W - 40, 40);
}
#endif

// —————————————— Tarefa de Leitura Serial (Core 0) ——————————————
void serialTask(void *pvParameters)
{
    Serial.println("serialTask running on Core 0");
    char localLineBuf[MAX_BUF];
    int localLinePos = 0;
    for (;;)
    {
        if (Serial.available())
        {
            char c = Serial.read();
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                localLineBuf[localLinePos] = '\0';
                xSemaphoreTake(xDataMutex, portMAX_DELAY);
                if (strncmp(localLineBuf, "SETUP:", 6) == 0)
                {
                    int max_r, yellow_s, red_s;
                    if (sscanf(localLineBuf + 6, "%d,%d,%d", &max_r, &yellow_s, &red_s) == 3)
                    {
                        vRpmMax = max_r;
                        vRpmYellowStart = yellow_s;
                        vRpmRedStart = red_s;
                        Serial.printf("SETUP received: Max=%d, Yellow=%d, Red=%d\n", vRpmMax, vRpmYellowStart, vRpmRedStart);
                        lastDataReceiveTime = millis();
                    }
                    else
                    {
                        Serial.println("Erro parse SETUP (serialTask)");
                    }
                }
                else if (strncmp(localLineBuf, "DATA:", 5) == 0)
                {
                    int r, s, g, abs_s, tc_s;
                    if (sscanf(localLineBuf + 5, "%d,%d,%d,%d,%d", &r, &s, &g, &abs_s, &tc_s) == 5)
                    {
                        currentRPM = constrain(r, 0, vRpmMax + 2000);
                        currentSpeed = constrain(s, 0, 999);
                        currentGear = constrain(g, -1, 9);
                        absState = constrain(abs_s, -1, 1);
                        tcState = constrain(tc_s, -1, 1);
                        lastDataReceiveTime = millis();
                    }
                    else
                    {
                        Serial.println("Erro parse DATA (serialTask)");
                    }
                }
                xSemaphoreGive(xDataMutex);
                localLinePos = 0;
            }
            else
            {
                if (localLinePos < MAX_BUF - 1)
                    localLineBuf[localLinePos++] = c;
                else
                    localLinePos = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// —————————————— Tarefa de Display (Core 1) ——————————————
void displayTask(void *pvParameters)
{
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

    for (;;)
    {
        uint32_t frameStart = millis();
        bool actualBlinkStateChanged = false;
        int local_currentRPM, local_currentSpeed, local_currentGear, local_absState, local_tcState;
        int local_vRpmMax_from_serial, local_vRpmYellowStart_from_serial, local_vRpmRedStart_from_serial;
        unsigned long current_time_millis = millis();

        if (current_time_millis - lastBlinkTime > BLINK_INTERVAL_MS)
        {
            bool oldHardwareBlinkState = blinkState;
            blinkState = !blinkState;
            lastBlinkTime = current_time_millis;
            if (oldHardwareBlinkState != blinkState)
                actualBlinkStateChanged = true;
        }

        xSemaphoreTake(xDataMutex, portMAX_DELAY);
        if ((current_time_millis - lastDataReceiveTime) > DATA_TIMEOUT_MS)
        {
            currentRPM = 0;
            currentSpeed = 0;
            currentGear = 0;
            absState = -1;
            tcState = -1;
        }
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
            local_vRpmRedStart_from_serial != prev_vRpmRedStart)
        {
            prev_vRpmMax = local_vRpmMax_from_serial;
            prev_vRpmYellowStart = local_vRpmYellowStart_from_serial;
            prev_vRpmRedStart = local_vRpmRedStart_from_serial;
            updateRpmDerivedParametersDisplay();
        }

        updateAndDrawBackgroundDisplay();

        drawArcRpmBar(local_currentRPM, prevRPM);
        drawRpmTextDisplay(local_currentRPM, prevRPM);
        drawSpeedDisplay(local_currentSpeed, prevSpeed);
        drawGearDisplay(local_currentGear, prevGear);
        drawIndicatorDisplay("TC", local_tcState, prevTcState, actualBlinkStateChanged && local_tcState == 1);
        drawIndicatorDisplay("ABS", local_absState, prevAbsState, actualBlinkStateChanged && local_absState == 1);

#if SHOW_FPS_COUNTER
        drawFpsCounter();
#endif

        prevRPM = local_currentRPM;
        prevSpeed = local_currentSpeed;
        prevGear = local_currentGear;
        prevAbsState = local_absState;
        prevTcState = local_tcState;
        backgroundChangedThisFrame = false;

        uint32_t frameTime = millis() - frameStart;
        if (frameTime < FRAME_DELAY_MS)
            vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS - frameTime));
        else
            vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// —————————————— ARDUINO setup() & loop() Principal ——————————————
void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(200);
    Serial.println("ESP32 Dashboard - Top Arc RPM (No Gradient) & FPS - Setup");

    lastDataReceiveTime = millis();

    xDataMutex = xSemaphoreCreateMutex();
    if (xDataMutex == NULL)
    {
        Serial.println("Erro Mutex!");
        while (1)
            ;
    }

    xTaskCreatePinnedToCore(displayTask, "DisplayTask", 12000, NULL, 2, &displayTaskHandle, APP_CPU_NUM);
    xTaskCreatePinnedToCore(serialTask, "SerialTask", 4000, NULL, 1, &serialTaskHandle, PRO_CPU_NUM);

    Serial.println("Tarefas (Display, Serial) criadas.");
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}