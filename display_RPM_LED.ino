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

// --- Configurações dos LEDs de RPM --- (NOVO)
static const int16_t NUM_RPM_LEDS = 7;
static const int16_t LED_RADIUS = 12;
static const int16_t LED_Y_POS = ACTUAL_CENTER_Y; // Posição Y dos LEDs
static const int16_t LED_SPACING = 32;            // Espaçamento entre os centros dos LEDs
static const int16_t LED_START_X = ACTUAL_CENTER_X - ((NUM_RPM_LEDS - 1) * LED_SPACING) / 2;

// Marcha
static const int16_t GEAR_Y_POS = ACTUAL_CENTER_Y + 45;

// --- Limites de RPM para Cores da Barra e Texto ---
volatile int vRpmMax = 8000;
// Os limites de amarelo/vermelho não são mais usados para a cor, mas podem ser mantidos se precisar
volatile int vRpmYellowStart = 4000;
volatile int vRpmRedStart = 6000;

// Cores para os LEDs de RPM (MODIFICADO)
const uint16_t COLOR_LED_GREEN = TFT_GREEN;
const uint16_t COLOR_LED_YELLOW = TFT_YELLOW;
const uint16_t COLOR_LED_RED = TFT_RED;
const uint16_t COLOR_LED_INACTIVE = TFT_DARKGREY;

static const int16_t INDICATOR_TEXT_Y_POS_OFFSET = 75;
static const int16_t INDICATOR_X_OFFSET = 55;
static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t FRAME_DELAY_MS = 1000 / 60; // Target 60 FPS

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

// --- Variáveis para Timeout de Dados ---
volatile unsigned long lastDataReceiveTime;
const unsigned long DATA_TIMEOUT_MS = 3000; // 3 segundos

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

// A função interpolateColor não é mais necessária para os LEDs, mas pode ser mantida para outros usos
uint16_t interpolateColor(uint16_t color1, uint16_t color2, float factor)
{
    if (factor <= 0.0)
        return color1;
    if (factor >= 1.0)
        return color2;
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;
    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;
    uint8_t r = r1 + (uint8_t)((float)(r2 - r1) * factor);
    uint8_t g = g1 + (uint8_t)((float)(g2 - g1) * factor);
    uint8_t b = b1 + (uint8_t)((float)(b2 - b1) * factor);
    return (r << 11) | (g << 5) | b;
}

// *********************************************************************************
// NOVA FUNÇÃO para desenhar os LEDs de RPM
// *********************************************************************************
// *********************************************************************************
// NOVA FUNÇÃO para desenhar os LEDs de RPM (COM LÓGICA "ALL RED")
// *********************************************************************************
void drawRpmLeds(int rpm_val, int prevRpmVal)
{
    int local_vRpmMax;
    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    local_vRpmMax = vRpmMax;
    xSemaphoreGive(xDataMutex);

    if (local_vRpmMax <= 0)
        return;

    // Calcula quantos LEDs devem estar acesos
    int leds_to_light = map(rpm_val, 0, local_vRpmMax, 0, NUM_RPM_LEDS);
    leds_to_light = constrain(leds_to_light, 0, NUM_RPM_LEDS);

    // Calcula o estado anterior para otimização
    int prev_leds_lit = (prevRpmVal < 0) ? -1 : map(prevRpmVal, 0, local_vRpmMax, 0, NUM_RPM_LEDS);
    prev_leds_lit = constrain(prev_leds_lit, 0, NUM_RPM_LEDS);

    // Se nada mudou e não é um redesenho completo, sai da função
    if (leds_to_light == prev_leds_lit && !backgroundChangedThisFrame && prevRpmVal != -1)
    {
        return;
    }

    // --- LÓGICA MODIFICADA COMEÇA AQUI ---
    // Verifica se estamos no modo "tudo vermelho" (quando o último LED é aceso)
    bool allRedMode = (leds_to_light == NUM_RPM_LEDS);

    // Loop para desenhar ou atualizar cada um dos 7 LEDs
    for (int i = 0; i < NUM_RPM_LEDS; i++)
    {
        uint16_t current_led_color;
        bool should_draw = false;

        // Determina a cor do LED atual
        if (i < leds_to_light)
        { // Se o LED deve estar aceso...
            if (allRedMode)
            {
                // MODO ESPECIAL: Se o último LED foi ativado, todos os LEDs acesos ficam vermelhos.
                current_led_color = COLOR_LED_RED;
            }
            else
            {
                // MODO NORMAL: Segue a progressão de cores.
                if (i < 4)
                { // Primeiros 4 LEDs são verdes
                    current_led_color = COLOR_LED_GREEN;
                }
                else if (i < 6)
                { // Próximos 2 são amarelos
                    current_led_color = COLOR_LED_YELLOW;
                }
                else
                { // O último LED é vermelho (neste caso, o 7º)
                    current_led_color = COLOR_LED_RED;
                }
            }
        }
        else
        { // Se o LED deve estar apagado...
            current_led_color = COLOR_LED_INACTIVE;
        }

        // Lógica de otimização: decide se este LED específico precisa ser redesenhado
        // Esta lógica precisa ser um pouco mais robusta para detectar a mudança de cor
        // de, por exemplo, amarelo para vermelho, mesmo que o LED continue aceso.
        if (backgroundChangedThisFrame || prevRpmVal == -1)
        {
            should_draw = true;
        }
        else
        {
            // Recalcula a cor anterior para uma comparação precisa
            bool prevAllRedMode = (prev_leds_lit == NUM_RPM_LEDS);
            uint16_t prev_led_color;
            if (i < prev_leds_lit)
            {
                if (prevAllRedMode)
                {
                    prev_led_color = COLOR_LED_RED;
                }
                else
                {
                    if (i < 4)
                        prev_led_color = COLOR_LED_GREEN;
                    else if (i < 6)
                        prev_led_color = COLOR_LED_YELLOW;
                    else
                        prev_led_color = COLOR_LED_RED;
                }
            }
            else
            {
                prev_led_color = COLOR_LED_INACTIVE;
            }

            if (current_led_color != prev_led_color)
            {
                should_draw = true;
            }
        }

        // Se o LED precisa ser desenhado, desenha o círculo
        if (should_draw)
        {
            int16_t x_pos = LED_START_X + i * LED_SPACING;
            tft.fillCircle(x_pos, LED_Y_POS, LED_RADIUS, current_led_color);
        }
    }
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
    int16_t text_w = 90;
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
    int16_t y_pos = ACTUAL_CENTER_Y + INDICATOR_TEXT_Y_POS_OFFSET;
    tft.fillRect(x_pos - text_w / 2, y_pos - text_h / 2, text_w, text_h, currentBgColor);
    tft.setTextColor(color, currentBgColor);
    tft.drawString(label, x_pos, y_pos);
}

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

        drawRpmTextDisplay(local_currentRPM, prevRPM);
        drawSpeedDisplay(local_currentSpeed, prevSpeed);

        // MODIFICADO: Chama a nova função de LEDs em vez da barra
        drawRpmLeds(local_currentRPM, prevRPM);

        drawGearDisplay(local_currentGear, prevGear);
        drawIndicatorDisplay("TC", local_tcState, prevTcState, actualBlinkStateChanged && local_tcState == 1);
        drawIndicatorDisplay("ABS", local_absState, prevAbsState, actualBlinkStateChanged && local_absState == 1);

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
    Serial.println("ESP32 Dashboard - RPM LEDs - Setup");

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