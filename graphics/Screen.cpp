#include <OLEDDisplay.h>
#include "graphics/ScreenFonts.h"
#include "graphics/images.h"

namespace graphics
{

#define IDLE_FRAMERATE 1
#define ACTIVE_FRAMERATE 10
#define BOOT_LOGO_TIMEOUT_MS 2500

static OLEDDisplay *display = nullptr;
static OLEDDisplayUi *ui = nullptr;
static uint32_t targetFramerate = IDLE_FRAMERATE;
static bool showingBootScreen = true;
static bool showingNormalScreen = false;
static char ourId[5];

enum FrameType {
    FRAME_MAIN_STATUS,
    FRAME_LORA_INFO,
    NUM_FRAMES
};
static FrameCallback normalFrames[NUM_FRAMES];
static uint8_t numNormalFrames = 0;

#define SCREEN_WIDTH (display ? display->getWidth() : 128)
#define SCREEN_HEIGHT (display ? display->getHeight() : 64)
#define getStringCenteredX(s) ((SCREEN_WIDTH - display->getStringWidth(s)) / 2)

static void drawIconScreen(OLEDDisplay *disp, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    disp->drawXbm(x + (SCREEN_WIDTH - icon_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - icon_height) / 2 + 2,
                     icon_width, icon_height, icon_bits);
    disp->setFont(FONT_MEDIUM);
    disp->setTextAlignment(TEXT_ALIGN_CENTER);
    disp->drawString(x + SCREEN_WIDTH / 2, y + SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM, "Meshtastic");
    disp->setFont(FONT_SMALL);
    disp->setTextAlignment(TEXT_ALIGN_RIGHT);
    disp->drawString(x + SCREEN_WIDTH, y + 0, ourId);
    disp->setTextAlignment(TEXT_ALIGN_LEFT);
}

static void drawMainStatusFrame(OLEDDisplay *disp, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    disp->setFont(FONT_SMALL);
    disp->setTextAlignment(TEXT_ALIGN_LEFT);
    char idStr[20];
    snprintf(idStr, sizeof(idStr), "ID: %s (%s)", owner.short_name ? owner.short_name : "NODE", ourId);
    disp->drawString(x + 1, y, idStr);
    disp->drawString(x + 1, y + FONT_HEIGHT_SMALL + 2, "Nodes: ?/?   Ch: #?");
    disp->drawString(x + 1, y + (FONT_HEIGHT_SMALL + 2) * 2, "Time: --:--  Batt: --%");
    disp->drawString(x + 1, y + (FONT_HEIGHT_SMALL + 2) * 3, "LoRa: OK / Mode?");
}

static void drawLoraInfoFrame(OLEDDisplay *disp, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    disp->setFont(FONT_MEDIUM);
    disp->setTextAlignment(TEXT_ALIGN_CENTER);
    disp->drawString(x + SCREEN_WIDTH / 2, y + 2, "LoRa Radio");
    disp->setFont(FONT_SMALL);
    disp->setTextAlignment(TEXT_ALIGN_LEFT);
    disp->drawString(x + 5, y + FONT_HEIGHT_MEDIUM + 5, "Freq: 433 MHz (Set)");
    disp->drawString(x + 5, y + FONT_HEIGHT_MEDIUM + FONT_HEIGHT_SMALL + 8, "Mode: Medium/Fast (Set)");
    disp->drawString(x + 5, y + FONT_HEIGHT_MEDIUM + (FONT_HEIGHT_SMALL * 2) + 11, "TX Power: Max (Set)");
    disp->drawString(x + 5, y + FONT_HEIGHT_MEDIUM + (FONT_HEIGHT_SMALL * 3) + 14, "Status: Initialized");
}

Screen::Screen(ScanI2C::DeviceAddress address, meshtastic_Config_DisplayConfig_OledType screenType, OLEDDISPLAY_GEOMETRY geometry)
    : concurrency::OSThread("Screen"), address_found(address), model(screenType), geometry(geometry), cmdQueue(4) {
     screen = this;
}

Screen::~Screen() {
    if (ui) delete ui;
    if (display) delete display;
}

void Screen::setup() {
    if (!address_found.address) return;
    display = new SH1106Wire(address_found.address, SDA_PIN, SCL_PIN, geometry);
    if (!display) return;
    ui = new OLEDDisplayUi(display);
    if (!ui) { delete display; display = nullptr; return; }
    cmdQueue.setReader(this);
    if (!ui->init()) { delete ui; delete display; ui = nullptr; display = nullptr; return; }
    useDisplay = true;
    ui->setTargetFPS(30);
    ui->disableAllIndicators();
    ui->setTimePerTransition(0);
    static FrameCallback bootFrames[] = {drawIconScreen};
    ui->setFrames(bootFrames, 1);
    if (config.display.flip_screen) display->flipScreenVertically();
    uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34};
    snprintf(ourId, sizeof(ourId), "%02X%02X", mac[4], mac[5]);
    handleSetOn(true);
    ui->update();
    showingBootScreen = true;
    showingNormalScreen = false;
    start();
}

void Screen::stopBootScreen() {
    if (!ui) return;
    showingBootScreen = false;
    showingNormalScreen = true;
    ui->setTimePerTransition(200);
    setFrames();
}

void Screen::handleSetOn(bool on) {
    if (!useDisplay || !display) return;
    if (on != screenOn) {
        if (on) display->displayOn();
        else display->displayOff();
        screenOn = on;
    }
    enabled = on;
    if(on) runASAP = true;
}

void Screen::setFrames() {
    numNormalFrames = 0;
    normalFrames[numNormalFrames++] = drawMainStatusFrame;
    normalFrames[numNormalFrames++] = drawLoraInfoFrame;
    ui->setFrames(normalFrames, numNormalFrames);
    ui->enableIndicator();
    ui->setIndicatorPosition(BOTTOM);
    ui->setIndicatorDirection(LEFT_RIGHT);
    ui->switchToFrame(0);
    setFastFramerate();
}

void Screen::setFastFramerate() {
    if (!ui) return;
    if (targetFramerate != ACTIVE_FRAMERATE) {
        targetFramerate = ACTIVE_FRAMERATE;
        ui->setTargetFPS(targetFramerate);
    }
    runASAP = true;
    setInterval(0);
}

void Screen::setIdleFramerate() {
    if (!ui) return;
    if (targetFramerate != IDLE_FRAMERATE) {
        targetFramerate = IDLE_FRAMERATE;
        ui->setTargetFPS(targetFramerate);
    }
}
}
