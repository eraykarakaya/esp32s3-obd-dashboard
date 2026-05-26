// ================================================================
//  Jetta 1.4 TSI — OBD2 Sportif Dashboard v7
//  Donanım : Waveshare ESP32-S3 Touch LCD 5" (1024x600)
//  Bağlantı: Vgate iCar Pro Bluetooth (BLE)
// ================================================================

#include <Arduino.h>
#include <math.h>
#include "lv_conf.h"
#include <Wire.h>
#include <TAMC_GT911.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <NimBLEDevice.h>

void parseOBD(String raw);
bool bleConnect();
void update_arcs();
void update_labels();
void sendNextPid();
void create_ui();
void play_startup_anim();
void apply_sport_theme(bool sport);
void play_sport_enter_anim();
void update_shift_light(int rpm);

// ── Ekran ────────────────────────────────────────────────────
#define SCREEN_W  1024
#define SCREEN_H  600

// TAM EKRAN BUFFER — full_refresh=1 ile tearing tamamen ortadan kalkar
// 1024*600*2 = 1.2MB, iki buffer = 2.4MB PSRAM (8MB'de sorun yok)
#define BUF_SIZE  (SCREEN_W * SCREEN_H)

#define TOUCH_SDA  9
#define TOUCH_SCL  8
#define TOUCH_INT  13
#define TOUCH_RST  38

TAMC_GT911 ts = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, SCREEN_W, SCREEN_H);

// Piksel saati 16→12MHz: elektriksel gürültüye daha dayanıklı
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    5, 3, 46, 7,
    1, 2, 42, 41, 40,
    39, 0, 45, 48, 47, 21,
    14, 38, 18, 17, 10,
    0, 40, 20, 40,
    0,  8,  3,  8,
    0, 12000000, false,   // 12MHz — daha stabil
    0, 0
);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(SCREEN_W, SCREEN_H, rgbpanel, 0, true);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

// ── BLE ──────────────────────────────────────────────────────
#define SVC_UUID  "E7810A71-73AE-499D-8C15-FAA9AEF0C3F2"
#define CHAR_UUID "BEF8D6C9-9C21-4C9E-B632-BD58C1009F9F"
#define SVC_ALT   "FFF0"
#define CHR_WR    "FFF2"
#define CHR_NTF   "FFF1"

static NimBLEClient               *bleClient  = nullptr;
static NimBLERemoteCharacteristic *writeChar  = nullptr;
static NimBLERemoteCharacteristic *notifyChar = nullptr;
static bool   bleConnected = false;
static String bleBuffer    = "";

struct OBDData {
    int  rpm      = 0;
    int  speed    = 0;
    int  load     = 0;
    int  dtcCount = 0;
    bool connected = false;
} obd;

// ── Renkler ───────────────────────────────────────────────────
#define C_BG         lv_color_hex(0x050508)
#define C_PANEL      lv_color_hex(0x0D0D14)
#define C_BDR        lv_color_hex(0x1A1A28)
#define C_NEON_RED   lv_color_hex(0xFF1A35)
#define C_NEON_BLUE  lv_color_hex(0x00E5FF)
#define C_ORANGE     lv_color_hex(0xFF6B1A)
#define C_WHITE      lv_color_hex(0xFFFFFF)
#define C_MUTED      lv_color_hex(0x555566)
#define C_GREEN      lv_color_hex(0x00E676)
#define C_YELLOW     lv_color_hex(0xFFE000)
#define C_SPORT_SPD  lv_color_hex(0xFF2800)
#define C_SPORT_TRB  lv_color_hex(0xFFD000)
#define C_SPORT_BDR  lv_color_hex(0xFF1A35)

// ── Shift Light — tek canvas (20 ayrı obje yerine) ────────────
#define SHIFT_BARS  20
#define SHIFT_W     1004
#define SHIFT_H     10
static lv_color_t  shift_cbuf[SHIFT_W * SHIFT_H];
lv_obj_t          *shift_canvas;

// ── Widget referansları ───────────────────────────────────────
lv_obj_t *scr_main;
lv_obj_t *meter_rpm;
lv_meter_indicator_t *indic_rpm;
lv_meter_scale_t     *scale_rpm;

lv_obj_t *arc_speed,      *arc_speed_peak, *lbl_speed, *lbl_speed_peak;
lv_obj_t *arc_turbo,      *arc_turbo_peak, *lbl_turbo, *lbl_turbo_peak;
lv_obj_t *lbl_rpm,        *lbl_stat;
lv_obj_t *bar_load,       *lbl_load_pct;
lv_obj_t *lbl_perf,       *lbl_sport_ind;
lv_obj_t *p_bot,          *sport_flash;

static int  peakSpeed = 0;
static int  peakBoost = 0;
static bool sportMode  = false;
static int  sportScore = 0;

// ── 0-100 Zamanlayıcı ─────────────────────────────────────────
// "accelStart" kullanıyoruz — "timerStart" ESP32 hw_timer API ile çakışıyor
enum AccelState { T_IDLE, T_RUNNING, T_DONE };
static AccelState accelState = T_IDLE;
static uint32_t   accelStart = 0;
static float      accelBest  = 0.0f;
static float      accelLast  = 0.0f;

// ── OBD PID ───────────────────────────────────────────────────
const char   *PIDS[]  = {"010C\r", "010D\r", "0104\r"};
const int     PID_N   = 3;
int           pidIdx  = 0;
unsigned long lastPid = 0;
const int     PID_MS  = 80;

// ================================================================
//  LVGL flush — full_refresh=1 ile frame başına 1 çağrı
// ================================================================
void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px) {
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px,
                            area->x2 - area->x1 + 1,
                            area->y2 - area->y1 + 1);
    lv_disp_flush_ready(drv);
}

void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    ts.read();
    if (ts.isTouched) {
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = ts.points[0].x;
        data->point.y = ts.points[0].y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ================================================================
//  BLE
// ================================================================
class BLECb : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *)    override { bleConnected = true;  obd.connected = true;  }
    void onDisconnect(NimBLEClient *) override {
        bleConnected = false; obd.connected = false;
        writeChar = notifyChar = nullptr;
    }
};

void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
    for (size_t i = 0; i < len; i++) {
        char ch = (char)data[i];
        if (ch == '\r' || ch == '\n' || ch == '>') {
            if (bleBuffer.length()) { parseOBD(bleBuffer); bleBuffer = ""; }
        } else { bleBuffer += ch; }
    }
}

static int hb(const String &s, int p) {
    if (p + 2 > (int)s.length()) return -1;
    return strtol(s.substring(p, p + 2).c_str(), nullptr, 16);
}

void parseOBD(String raw) {
    raw.trim(); raw.replace(" ", "");
    if (raw.length() < 6 || raw.startsWith("NO") || raw.startsWith("?")) return;
    int idx = raw.indexOf("41");
    if (idx < 0) return;
    raw = raw.substring(idx);
    String pid = raw.substring(2, 4);
    int A = hb(raw, 4), B = hb(raw, 6);
    if (A < 0) return;
    if      (pid == "0C") obd.rpm      = ((A << 8) | B) / 4;
    else if (pid == "0D") obd.speed    = A;
    else if (pid == "04") obd.load     = A * 100 / 255;
    else if (pid == "01") obd.dtcCount = A & 0x7F;
}

bool bleConnect() {
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    scan->start(2, false);
    NimBLEScanResults res = scan->getResults();
    NimBLEAdvertisedDevice *tgt = nullptr;
    for (int i = 0; i < res.getCount(); i++) {
        auto dev = res.getDevice(i);
        String nm = dev.getName().c_str(); nm.toUpperCase();
        if (nm.indexOf("VLINK") >= 0 || nm.indexOf("ICAR") >= 0 ||
            nm.indexOf("ELM")  >= 0 || nm.indexOf("OBD")  >= 0) {
            tgt = new NimBLEAdvertisedDevice(dev); break;
        }
    }
    scan->stop();
    if (!tgt) return false;

    bleClient = NimBLEDevice::createClient();
    bleClient->setClientCallbacks(new BLECb(), false);
    if (!bleClient->connect(tgt)) { delete tgt; return false; }

    NimBLERemoteService *svc = bleClient->getService(SVC_UUID);
    if (!svc) svc = bleClient->getService(SVC_ALT);
    if (!svc) { bleClient->disconnect(); delete tgt; return false; }

    writeChar  = svc->getCharacteristic(CHAR_UUID);
    notifyChar = svc->getCharacteristic(CHAR_UUID);
    if (!writeChar) {
        writeChar  = svc->getCharacteristic(CHR_WR);
        notifyChar = svc->getCharacteristic(CHR_NTF);
    }
    if (!writeChar || !notifyChar) { bleClient->disconnect(); delete tgt; return false; }
    if (notifyChar->canNotify()) notifyChar->subscribe(true, onNotify);

    delay(200);
    writeChar->writeValue("ATZ\r",   4); delay(600);
    writeChar->writeValue("ATE0\r",  5); delay(100);
    writeChar->writeValue("ATL0\r",  5); delay(100);
    writeChar->writeValue("ATSP0\r", 6); delay(300);
    delete tgt;
    return true;
}

void sendNextPid() {
    if (!bleConnected || !writeChar) return;
    if (millis() - lastPid < (unsigned long)PID_MS) return;
    lastPid = millis();
    writeChar->writeValue((uint8_t *)PIDS[pidIdx], strlen(PIDS[pidIdx]), false);
    pidIdx = (pidIdx + 1) % PID_N;
}

TaskHandle_t bleTaskHandle;
void bleBackgroundWorker(void *) {
    // Açılış animasyonu tamamlanana kadar bekle — ilk render smooth gitsin
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    for (;;) {
        if (!bleConnected) { bleConnect(); vTaskDelay(2000 / portTICK_PERIOD_MS); }
        else               { sendNextPid(); vTaskDelay(20   / portTICK_PERIOD_MS); }
    }
}

// ================================================================
//  UI yardımcıları
// ================================================================
lv_obj_t *mk_panel(lv_obj_t *par, int x, int y, int w, int h) {
    lv_obj_t *p = lv_obj_create(par);
    lv_obj_set_pos(p, x, y); lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, C_PANEL, 0);
    lv_obj_set_style_border_color(p, C_BDR, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *mk_label(lv_obj_t *par, const char *txt, int x, int y,
                   const lv_font_t *font, lv_color_t col) {
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t *mk_arc(lv_obj_t *par, int cx, int cy, int sz,
                 int rmin, int rmax,
                 lv_color_t trackCol, lv_color_t indCol,
                 int trackW, int indW) {
    lv_obj_t *a = lv_arc_create(par);
    lv_obj_set_size(a, sz, sz);
    lv_obj_set_pos(a, cx - sz / 2, cy - sz / 2);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, rmin, rmax);
    lv_arc_set_value(a, rmin);
    lv_obj_set_style_arc_color(a, trackCol, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, indCol,   LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(a, trackW, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, indW,   LV_PART_INDICATOR);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a, 0, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    return a;
}

lv_obj_t *mk_arc_label(lv_obj_t *arc, const char *txt, int dy,
                       const lv_font_t *font, lv_color_t col, int fixedW = 0) {
    lv_obj_t *l = lv_label_create(arc);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    if (fixedW > 0) {
        lv_obj_set_width(l, fixedW);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_align(l, LV_ALIGN_CENTER, 0, dy);
    return l;
}

// ================================================================
//  Shift Light — tek canvas objesi (20 obje değil = çok daha hızlı)
// ================================================================
void update_shift_light(int rpm) {
    static int lastRpm = -999;
    if (abs(rpm - lastRpm) < 50) return;  // 50 RPM eşiği — gereksiz yeniden çizimi önler
    lastRpm = rpm;

    int lit = map(constrain(rpm, 0, 7000), 0, 7000, 0, SHIFT_BARS);
    int barW = SHIFT_W / SHIFT_BARS - 2;

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius = 3;

    // Önce arka planı temizle
    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = C_BG;
    bg.bg_opa   = LV_OPA_COVER;
    lv_canvas_draw_rect(shift_canvas, 0, 0, SHIFT_W, SHIFT_H, &bg);

    for (int i = 0; i < SHIFT_BARS; i++) {
        int x = i * (barW + 2);
        if (i < lit) {
            rd.bg_color = (i < 10) ? C_GREEN : (i < 16) ? C_YELLOW : C_NEON_RED;
            rd.bg_opa   = LV_OPA_COVER;
        } else {
            rd.bg_color = C_BDR;
            rd.bg_opa   = 35;
        }
        lv_canvas_draw_rect(shift_canvas, x, 0, barW, SHIFT_H, &rd);
    }
}

// ================================================================
//  Animasyonlar
// ================================================================
static void set_rpm_anim_cb(void *var, int32_t v) {
    lv_meter_set_indicator_value(meter_rpm, (lv_meter_indicator_t *)var, v);
}

void play_startup_anim() {
    // Ekran tam render olduktan sonra başlat — 400ms bekle
     lv_timer_handler(); lv_timer_handler(); // çift flush: tüm widget'lar çizilsin
    delay(400);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_rpm_anim_cb);
    lv_anim_set_var(&a, indic_rpm);
    lv_anim_set_time(&a, 1400);        // biraz daha yavaş = smooth
    lv_anim_set_values(&a, 0, 7000);
    lv_anim_set_playback_time(&a, 1200);
    lv_anim_set_playback_delay(&a, 200);
    lv_anim_start(&a);
}

static void flash_opa_cb(void *var, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

void play_sport_enter_anim() {
    lv_obj_move_foreground(sport_flash);
    lv_obj_set_style_bg_opa(sport_flash, 0, 0);

    lv_anim_t fa;
    lv_anim_init(&fa);
    lv_anim_set_exec_cb(&fa, flash_opa_cb);
    lv_anim_set_var(&fa, sport_flash);
    lv_anim_set_values(&fa, 0, 100);
    lv_anim_set_time(&fa, 100);
    lv_anim_set_playback_time(&fa, 350);
    lv_anim_set_playback_delay(&fa, 30);
    lv_anim_start(&fa);

    lv_anim_t ra;
    lv_anim_init(&ra);
    lv_anim_set_exec_cb(&ra, set_rpm_anim_cb);
    lv_anim_set_var(&ra, indic_rpm);
    lv_anim_set_values(&ra, constrain(obd.rpm, 0, 7000), 7000);
    lv_anim_set_time(&ra, 320);
    lv_anim_set_playback_time(&ra, 280);
    lv_anim_set_playback_delay(&ra, 0);
    lv_anim_start(&ra);
}

void apply_sport_theme(bool sport) {
    if (sport) {
        lv_obj_set_style_arc_color(arc_speed, C_SPORT_SPD, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_turbo, C_SPORT_TRB, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_speed, lv_color_hex(0x3A0800), LV_PART_MAIN);
        lv_obj_set_style_border_color(meter_rpm, C_SPORT_BDR, 0);
        lv_obj_set_style_border_width(meter_rpm, 3, 0);
        lv_obj_set_style_border_color(p_bot, C_SPORT_BDR, 0);
        lv_obj_set_style_border_width(p_bot, 2, 0);
        lv_label_set_text(lbl_sport_ind, "[ S ]");
        lv_obj_set_style_text_color(lbl_sport_ind, C_SPORT_BDR, 0);
    } else {
        lv_obj_set_style_arc_color(arc_speed, C_NEON_BLUE, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_turbo, C_NEON_BLUE, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_speed, C_BDR, LV_PART_MAIN);
        lv_obj_set_style_border_color(meter_rpm, C_BDR, 0);
        lv_obj_set_style_border_width(meter_rpm, 2, 0);
        lv_obj_set_style_border_color(p_bot, C_BDR, 0);
        lv_obj_set_style_border_width(p_bot, 1, 0);
        lv_label_set_text(lbl_sport_ind, "");
    }
}

// ================================================================
//  Ana ekranı oluştur
// ================================================================
void create_ui() {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, C_BG, 0);
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    // ── SHIFT LIGHT — tek canvas ───────────────────────────
    shift_canvas = lv_canvas_create(scr_main);
    lv_canvas_set_buffer(shift_canvas, shift_cbuf, SHIFT_W, SHIFT_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(shift_canvas, 10, 6);
    lv_canvas_fill_bg(shift_canvas, C_BG, LV_OPA_COVER);

    // ── ORTA: DEVİR SAATİ ──────────────────────────────────
    meter_rpm = lv_meter_create(scr_main);
    lv_obj_set_size(meter_rpm, 380, 380);
    lv_obj_align(meter_rpm, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(meter_rpm, C_PANEL, 0);
    lv_obj_set_style_border_color(meter_rpm, C_BDR, 0);
    lv_obj_set_style_border_width(meter_rpm, 2, 0);
    lv_obj_set_style_pad_all(meter_rpm, 12, 0);

    scale_rpm = lv_meter_add_scale(meter_rpm);
    lv_meter_set_scale_ticks(meter_rpm, scale_rpm, 71, 2, 10, C_MUTED);
    lv_meter_set_scale_major_ticks(meter_rpm, scale_rpm, 10, 4, 18, C_WHITE, 14);
    lv_meter_set_scale_range(meter_rpm, scale_rpm, 0, 8000, 270, 135);

    lv_meter_indicator_t *rl = lv_meter_add_arc(meter_rpm, scale_rpm, 6, C_NEON_RED, 0);
    lv_meter_set_indicator_start_value(meter_rpm, rl, 5500);
    lv_meter_set_indicator_end_value(meter_rpm, rl, 7000);

    indic_rpm = lv_meter_add_needle_line(meter_rpm, scale_rpm, 5, C_NEON_RED, -18);

    lbl_rpm = mk_label(meter_rpm, "0", 0, 0, &lv_font_montserrat_32, C_WHITE);
    lv_obj_align(lbl_rpm, LV_ALIGN_CENTER, 0, 52);
    lv_obj_t *u = mk_label(meter_rpm, "RPM", 0, 0, &lv_font_montserrat_14, C_NEON_RED);
    lv_obj_align(u, LV_ALIGN_CENTER, 0, 78);

    // ── SOL: HIZ ARC ───────────────────────────────────────
    arc_speed = mk_arc(scr_main, 148, 285, 230, 0, 240, C_BDR, C_NEON_BLUE, 8, 20);
    arc_speed_peak = mk_arc(scr_main, 148, 285, 230, 0, 240,
                            lv_color_hex(0x000000), C_WHITE, 0, 4);
    mk_arc_label(arc_speed, "HIZ",   -58, &lv_font_montserrat_14, C_NEON_BLUE);
    lbl_speed      = mk_arc_label(arc_speed, "0",     -10, &lv_font_montserrat_32, C_WHITE, 100);
    mk_arc_label(arc_speed, "km/h",  30,  &lv_font_montserrat_14, C_MUTED);
    lbl_speed_peak = mk_arc_label(arc_speed, "MAX 0", 54,  &lv_font_montserrat_12, C_MUTED, 100);

    // ── SAĞ: TURBO ARC — renk bölgeli ──────────────────────
    // Dekoratif bölge arları (ince, sabit)
    auto make_zone = [](int cx, int cy, int sz, int a1, int a2, lv_color_t col) {
        lv_obj_t *z = lv_arc_create(lv_scr_act());
        lv_obj_set_size(z, sz, sz);
        lv_obj_set_pos(z, cx - sz / 2, cy - sz / 2);
        lv_arc_set_rotation(z, 135);
        lv_arc_set_bg_angles(z, a1, a2);
        lv_obj_set_style_arc_color(z, col, LV_PART_MAIN);
        lv_obj_set_style_arc_width(z, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(z, 110, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(z, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(z, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(z, 0, 0);
        lv_obj_set_style_pad_all(z, 0, 0);
        lv_obj_remove_style(z, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(z, LV_OBJ_FLAG_CLICKABLE);
    };
    make_zone(876, 285, 230,   0,  90, C_GREEN);    // 0.0–0.5 bar yeşil
    make_zone(876, 285, 230,  90, 180, C_ORANGE);   // 0.5–1.0 bar turuncu
    make_zone(876, 285, 230, 180, 270, C_NEON_RED); // 1.0–1.5 bar kırmızı

    arc_turbo = mk_arc(scr_main, 876, 285, 230, 0, 150, C_BDR, C_NEON_BLUE, 8, 20);
    arc_turbo_peak = mk_arc(scr_main, 876, 285, 230, 0, 150,
                            lv_color_hex(0x000000), C_WHITE, 0, 4);
    mk_arc_label(arc_turbo, "TURBO",    -58, &lv_font_montserrat_14, C_NEON_BLUE);
    lbl_turbo      = mk_arc_label(arc_turbo, "0.0",     -10, &lv_font_montserrat_32, C_WHITE, 100);
    mk_arc_label(arc_turbo, "BAR",       30, &lv_font_montserrat_14, C_MUTED);
    lbl_turbo_peak = mk_arc_label(arc_turbo, "PEAK 0.0", 54, &lv_font_montserrat_12, C_MUTED, 110);

    // ── ALT ÇUBUK ──────────────────────────────────────────
    p_bot = mk_panel(scr_main, 10, 524, 1004, 66);

    mk_label(p_bot, "MOTOR YUKU", 14, 8, &lv_font_montserrat_12, C_MUTED);

    bar_load = lv_bar_create(p_bot);
    lv_obj_set_size(bar_load, 200, 8);
    lv_obj_set_pos(bar_load, 14, 32);
    lv_bar_set_range(bar_load, 0, 100);
    lv_bar_set_value(bar_load, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_load, C_BDR,       LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_load, C_NEON_BLUE,  LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_load, LV_OPA_COVER,  LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_load, LV_OPA_COVER,  LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_load, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_load, 4, LV_PART_INDICATOR);

    lbl_load_pct = mk_label(p_bot, "0%", 222, 26, &lv_font_montserrat_12, C_NEON_BLUE);

    lbl_sport_ind = lv_label_create(p_bot);
    lv_label_set_text(lbl_sport_ind, "");
    lv_obj_set_style_text_font(lbl_sport_ind,  &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sport_ind, C_NEON_RED, 0);
    lv_obj_set_style_bg_opa(lbl_sport_ind, LV_OPA_TRANSP, 0);
    lv_obj_align(lbl_sport_ind, LV_ALIGN_CENTER, 0, -12);

    lbl_stat = lv_label_create(p_bot);
    lv_label_set_text(lbl_stat, "OBD ARANIYOR...");
    lv_obj_set_style_text_font(lbl_stat,  &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_stat, C_MUTED, 0);
    lv_obj_set_style_bg_opa(lbl_stat, LV_OPA_TRANSP, 0);
    lv_obj_align(lbl_stat, LV_ALIGN_CENTER, 0, 10);

    lbl_perf = lv_label_create(p_bot);
    lv_label_set_text(lbl_perf, "0-100: --.-s  -- HP");
    lv_obj_set_style_text_font(lbl_perf,  &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_perf, C_MUTED, 0);
    lv_obj_set_style_bg_opa(lbl_perf, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_align(lbl_perf, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_perf, LV_ALIGN_RIGHT_MID, -14, 0);

    // Sport flash overlay
    sport_flash = lv_obj_create(scr_main);
    lv_obj_set_size(sport_flash, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(sport_flash, 0, 0);
    lv_obj_set_style_bg_color(sport_flash, C_NEON_RED, 0);
    lv_obj_set_style_bg_opa(sport_flash, 0, 0);
    lv_obj_set_style_border_width(sport_flash, 0, 0);
    lv_obj_set_style_radius(sport_flash, 0, 0);
    lv_obj_clear_flag(sport_flash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(sport_flash, LV_OBJ_FLAG_SCROLLABLE);

    lv_scr_load(scr_main);
    play_startup_anim();
}

// ================================================================
//  HIZLI güncelleme (40ms) — arc değerleri
// ================================================================
void update_arcs() {
    static int c_rpm = -1, c_speed = -1, c_boost = -1;

    // RPM — 50 RPM eşiği: daha az meter yeniden çizimi
    int newRpm = constrain(obd.rpm, 0, 7000);
    if (abs(newRpm - c_rpm) > 50) {
        c_rpm = newRpm;
        lv_meter_set_indicator_value(meter_rpm, indic_rpm, newRpm);
    }
    update_shift_light(obd.rpm);

    // Hız arc
    int sp = constrain(obd.speed, 0, 240);
    if (sp != c_speed) {
        c_speed = sp;
        lv_arc_set_value(arc_speed, sp);
        if (sp > peakSpeed) {
            peakSpeed = sp;
            lv_arc_set_value(arc_speed_peak, peakSpeed);
        }
    }

    // Turbo boost + renk bölgesi
    int boost = constrain((obd.load * 150) / 100, 0, 150);
    if (boost != c_boost) {
        c_boost = boost;
        lv_arc_set_value(arc_turbo, boost);
        if (boost > peakBoost) {
            peakBoost = boost;
            lv_arc_set_value(arc_turbo_peak, peakBoost);
        }
        // Renk: bölgeye göre değişir
        lv_color_t tc = (boost > 100) ? C_NEON_RED
                      : (boost > 50)  ? C_ORANGE
                                      : C_NEON_BLUE;
        lv_obj_set_style_arc_color(arc_turbo, tc, LV_PART_INDICATOR);
    }
}

// ================================================================
//  YAVAŞ güncelleme (150ms) — metin etiketleri
// ================================================================
void update_labels() {
    char buf[48];

    static int   c_rpm_lbl  = -1, c_speed_lbl = -1, c_boost_lbl = -1;
    static int   c_load = -1, c_load_tier = -1, c_hp = -1;
    static float c_adisp    = -1.0f;
    static bool  c_conn     = false;
    static int   c_peak_spd = -1, c_peak_bst  = -1;

    if (abs(obd.rpm - c_rpm_lbl) > 50) {
        c_rpm_lbl = obd.rpm;
        snprintf(buf, sizeof(buf), "%d", obd.rpm);
        lv_label_set_text(lbl_rpm, buf);
    }
    if (obd.speed != c_speed_lbl) {
        c_speed_lbl = obd.speed;
        snprintf(buf, sizeof(buf), "%d", obd.speed);
        lv_label_set_text(lbl_speed, buf);
    }
    if (peakSpeed != c_peak_spd) {
        c_peak_spd = peakSpeed;
        snprintf(buf, sizeof(buf), "MAX %d", peakSpeed);
        lv_label_set_text(lbl_speed_peak, buf);
    }

    int boost = constrain((obd.load * 150) / 100, 0, 150);
    if (boost != c_boost_lbl) {
        c_boost_lbl = boost;
        snprintf(buf, sizeof(buf), "%.1f", boost / 100.0f);
        lv_label_set_text(lbl_turbo, buf);
    }
    if (peakBoost != c_peak_bst) {
        c_peak_bst = peakBoost;
        snprintf(buf, sizeof(buf), "PEAK %.1f", peakBoost / 100.0f);
        lv_label_set_text(lbl_turbo_peak, buf);
    }

    if (obd.load != c_load) {
        c_load = obd.load;
        int v = constrain(obd.load, 0, 100);
        lv_bar_set_value(bar_load, v, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", v);
        lv_label_set_text(lbl_load_pct, buf);
    }
    {
        int v    = constrain(obd.load, 0, 100);
        int tier = (v > 80) ? 2 : (v > 55) ? 1 : 0;
        if (tier != c_load_tier) {
            c_load_tier = tier;
            lv_color_t col = (tier == 2) ? C_NEON_RED
                           : (tier == 1) ? C_ORANGE
                                         : C_NEON_BLUE;
            lv_obj_set_style_bg_color(bar_load, col, LV_PART_INDICATOR);
            lv_obj_set_style_text_color(lbl_load_pct, col, 0);
        }
    }

    if (obd.connected != c_conn) {
        c_conn = obd.connected;
        if (obd.connected) {
            if (!sportMode) {
                lv_label_set_text(lbl_stat, "BLE BAGLANDI");
                lv_obj_set_style_text_color(lbl_stat, C_NEON_BLUE, 0);
            }
            peakSpeed = 0; peakBoost = 0;
        } else {
            lv_label_set_text(lbl_stat, "OBD ARANIYOR...");
            lv_obj_set_style_text_color(lbl_stat, C_MUTED, 0);
        }
    }

    // 0-100 + HP
    int estHP = (obd.rpm > 800 && obd.load > 0)
                ? (int)((float)obd.rpm / 6200.0f * (float)obd.load / 100.0f * 122.0f)
                : 0;

    switch (accelState) {
        case T_IDLE:
            if (obd.speed >= 5 && obd.speed <= 15 && obd.load > 70) {
                accelState = T_RUNNING;
                accelStart = millis();
            }
            break;
        case T_RUNNING: {
            float el = (millis() - accelStart) / 1000.0f;
            if (obd.speed >= 100) {
                accelLast = el; accelState = T_DONE;
                if (accelBest == 0.0f || el < accelBest) accelBest = el;
            } else if (el > 25.0f || obd.speed < 3) {
                accelState = T_IDLE;
            }
            break;
        }
        case T_DONE:
            if (millis() - accelStart > (uint32_t)((accelLast + 4.0f) * 1000.0f))
                accelState = T_IDLE;
            break;
    }

    float adisp = (accelState == T_RUNNING) ? (millis() - accelStart) / 1000.0f : accelLast;
    if (estHP != c_hp || fabsf(adisp - c_adisp) > 0.09f) {
        c_hp = estHP; c_adisp = adisp;
        char tStr[22];
        if (accelState == T_RUNNING)
            snprintf(tStr, sizeof(tStr), "%.1fs...", adisp);
        else if (accelLast > 0.0f) {
            if (accelBest > 0.0f && accelBest < accelLast)
                snprintf(tStr, sizeof(tStr), "%.2fs(en:%.2f)", accelLast, accelBest);
            else
                snprintf(tStr, sizeof(tStr), "%.2fs", accelLast);
        } else {
            snprintf(tStr, sizeof(tStr), "--.-s");
        }
        snprintf(buf, sizeof(buf), "0-100: %s  %dHP", tStr, estHP);
        lv_label_set_text(lbl_perf, buf);
        lv_color_t pc = (accelState == T_RUNNING) ? C_ORANGE
                      : (accelState == T_DONE)    ? C_GREEN
                      : (estHP > 80)              ? C_NEON_BLUE
                                                  : C_MUTED;
        lv_obj_set_style_text_color(lbl_perf, pc, 0);
    }

    // Sport mod
    bool sportCond = obd.connected && (
        sportMode ? (obd.speed > 65 || obd.rpm > 2600)
                  : (obd.speed > 80 || obd.rpm > 3200)
    );
    sportScore = sportCond ? min(sportScore + 1, 40) : max(sportScore - 1, 0);
    bool newSport = (sportScore > 20);
    if (newSport != sportMode) {
        sportMode = newSport;
        apply_sport_theme(sportMode);
        if (sportMode) {
            play_sport_enter_anim();
            lv_label_set_text(lbl_stat, "SPORT MODU");
            lv_obj_set_style_text_color(lbl_stat, C_SPORT_BDR, 0);
        } else if (obd.connected) {
            lv_label_set_text(lbl_stat, "BLE BAGLANDI");
            lv_obj_set_style_text_color(lbl_stat, C_NEON_BLUE, 0);
        }
    }
}

// ================================================================
//  Setup & Loop
// ================================================================
void setup() {
    Serial.begin(115200);

    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    pinMode(TOUCH_INT, INPUT_PULLUP);
    Wire.beginTransmission(0x24); Wire.write(0x01); Wire.endTransmission();
    Wire.beginTransmission(0x38); Wire.write(0x0E); Wire.endTransmission();
    delay(200);

    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);  delay(10);
    digitalWrite(TOUCH_RST, HIGH); delay(100);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);

    if (!gfx->begin()) while (1) delay(1000);
    gfx->fillScreen(BLACK);

    // TAM EKRAN PSRAM BUFFER — tearing'i önler
    buf1 = (lv_color_t *)ps_malloc(BUF_SIZE * sizeof(lv_color_t));
    buf2 = (lv_color_t *)ps_malloc(BUF_SIZE * sizeof(lv_color_t));
    if (!buf1 || !buf2) while (1) delay(1000);  // PSRAM yoksa dur

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = SCREEN_W;
    disp_drv.ver_res      = SCREEN_H;
    disp_drv.flush_cb     = disp_flush;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.full_refresh = 1;   // Her frame tam ekran — tearing yok
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    create_ui();

    NimBLEDevice::init("JettaDash");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    xTaskCreatePinnedToCore(bleBackgroundWorker, "BLE_Task", 8192, NULL, 1, &bleTaskHandle, 0);
}

static uint32_t      lastTick   = 0;
static unsigned long lastFastUI = 0;
static unsigned long lastSlowUI = 0;

void loop() {
    uint32_t now = millis();
    lv_tick_inc(now - lastTick);
    lastTick = now;

    uint32_t lvgl_delay = lv_timer_handler();  // LVGL ne kadar beklememizi söylüyor

    if (now - lastFastUI > 40) {    // Arc: 40ms = 25fps
        lastFastUI = now;
        update_arcs();
    }
    if (now - lastSlowUI > 150) {   // Etiket/durum: 150ms
        lastSlowUI = now;
        update_labels();
    }

    // LVGL'nin önerdiği süre kadar bekle (max 5ms) — CPU boşa dönmesin
    uint32_t wait = min(lvgl_delay, (uint32_t)5);
    if (wait > 1) delay(wait);
}
