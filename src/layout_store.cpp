#include "layout_store.h"
#include "gfx_util.h"

#include <LittleFS.h>

namespace Layout {

namespace {

JsonDocument s_doc;
bool         s_ready = false;
char         s_path[80] = "";

/**
 * Имя раздела из partitions.csv.
 *
 * ВАЖНО: LittleFS.begin() по умолчанию ищет раздел с меткой "spiffs", а у нас
 * он назван "littlefs" (SubType при этом spiffs). Без явной метки монтирование
 * падает, хотя образ залит корректно — PlatformIO выбирает раздел для uploadfs
 * по SubType, а не по имени. Симптом: "NO LAYOUT" на экране после успешного
 * uploadfs.
 */
constexpr const char* kPartLabel = "littlefs";

uint16_t themeColor(const char* key, uint16_t fallback) {
    if (!s_ready) return fallback;
    return rgb565FromHex(s_doc["theme"][key] | static_cast<const char*>(nullptr), fallback);
}

JsonObjectConst screenAt(uint8_t idx) {
    if (!s_ready) return JsonObjectConst();
    JsonArrayConst screens = s_doc["screens"].as<JsonArrayConst>();
    if (idx >= screens.size()) return JsonObjectConst();
    return screens[idx].as<JsonObjectConst>();
}

bool tryLoad(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    const size_t size = f.size();
    const DeserializationError err = deserializeJson(s_doc, f);
    f.close();

    if (err) {
        Serial.printf("[layout] %s: ошибка разбора (%s)\n", path, err.c_str());
        return false;
    }

    s_ready = true;
    strncpy(s_path, path, sizeof s_path - 1);
    s_path[sizeof s_path - 1] = '\0';

    Serial.printf("[layout] загружен %s (%u байт), экранов: %u, sim: %s\n",
                  path, static_cast<unsigned>(size), screenCount(),
                  simEnabled() ? "вкл" : "выкл");
    return true;
}

/// Собрать полный путь: File::name() в core 3.x возвращает имя без каталога,
/// но на всякий случай обрабатываем и абсолютный вариант.
void joinLayoutPath(const char* name, char* out, size_t outSize) {
    if (name && name[0] == '/') snprintf(out, outSize, "%s", name);
    else                        snprintf(out, outSize, "/layouts/%s", name ? name : "");
}

void listLayouts() {
    Serial.printf("[fs] littlefs: занято %u КБ из %u КБ\n",
                  static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                  static_cast<unsigned>(LittleFS.totalBytes() / 1024));

    File dir = LittleFS.open("/layouts");
    if (!dir || !dir.isDirectory()) {
        Serial.println("[fs] каталога /layouts нет");
        return;
    }

    Serial.println("[fs] доступные лейауты:");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;
        char path[80];
        joinLayoutPath(f.name(), path, sizeof path);
        Serial.printf("[fs]   %s (%u байт)\n", path, static_cast<unsigned>(f.size()));
    }
}

/// Явный выбор через /device.json: { "layout": "/layouts/xxx.json" }
bool loadFromDeviceConfig() {
    File f = LittleFS.open("/device.json", "r");
    if (!f) return false;

    JsonDocument cfg;
    const DeserializationError err = deserializeJson(cfg, f);
    f.close();

    if (err) {
        Serial.printf("[layout] /device.json: ошибка разбора (%s)\n", err.c_str());
        return false;
    }

    const char* p = cfg["layout"] | static_cast<const char*>(nullptr);
    if (!p || !*p) return false;

    Serial.printf("[layout] /device.json выбирает %s\n", p);
    return tryLoad(p);
}

/// Последний шанс: первый же *.json в /layouts, чтобы устройство ожило
/// даже когда ни device.json, ни active.json не заданы.
bool loadFirstInDir() {
    File dir = LittleFS.open("/layouts");
    if (!dir || !dir.isDirectory()) return false;

    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) continue;

        const char* n = f.name();
        const size_t len = n ? strlen(n) : 0;
        if (len < 6 || strcmp(n + len - 5, ".json") != 0) continue;

        char path[80];
        joinLayoutPath(n, path, sizeof path);
        if (tryLoad(path)) return true;
    }
    return false;
}

} // namespace

bool load() {
    if (!LittleFS.begin(false, "/littlefs", 10, kPartLabel)) {
        Serial.printf("[layout] раздел \"%s\" не смонтирован\n", kPartLabel);
        Serial.println("[layout] залей образ: pio run -e dash -t uploadfs");
        return false;
    }

    listLayouts();

    // Приоритет: явное указание → active.json → что найдётся
    if (loadFromDeviceConfig())           return true;
    if (tryLoad("/layouts/active.json"))  return true;
    if (loadFirstInDir())                 return true;

    Serial.println("[layout] в /layouts не нашлось пригодного json");
    return false;
}

bool simEnabled() {
    if (!s_ready) return false;
    return s_doc["sim"]["enabled"] | false;
}

uint8_t screenCount() {
    if (!s_ready) return 0;
    return static_cast<uint8_t>(s_doc["screens"].as<JsonArrayConst>().size());
}

const char* screenName(uint8_t idx) {
    JsonObjectConst s = screenAt(idx);
    if (s.isNull()) return "";
    return s["name"] | s["id"] | "";
}

JsonArrayConst widgets(uint8_t idx) {
    JsonObjectConst s = screenAt(idx);
    if (s.isNull()) return JsonArrayConst();
    return s["widgets"].as<JsonArrayConst>();
}

uint16_t screenBg(uint8_t idx) {
    const uint16_t themeBg = themeColor("bg", 0x0000);
    JsonObjectConst s = screenAt(idx);
    if (s.isNull()) return themeBg;
    return rgb565FromHex(s["bg"] | static_cast<const char*>(nullptr), themeBg);
}

uint16_t themeFg()    { return themeColor("fg",    0xFFFF); }
uint16_t themeMuted() { return themeColor("muted", 0x6B4D); }
uint16_t themeWarn()  { return themeColor("warn",  0xFE60); }
uint16_t themeCrit()  { return themeColor("crit",  0xF1C7); }

const char* loadedFrom() { return s_path; }

} // namespace Layout
