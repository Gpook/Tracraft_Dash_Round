#include "signal_bus.h"

#include <string.h>

namespace Signals {

namespace {

struct Entry {
    char  id[kMaxIdLen];
    float value;
};

Entry  s_entries[kMaxSignals];
uint8_t s_count = 0;

/// Линейный поиск: сигналов десятки, хеш-таблица тут только добавила бы кода.
int find(const char* id) {
    for (uint8_t i = 0; i < s_count; ++i) {
        if (strncmp(s_entries[i].id, id, kMaxIdLen) == 0) return i;
    }
    return -1;
}

} // namespace

void set(const char* id, float value) {
    if (!id || !*id) return;

    const int idx = find(id);
    if (idx >= 0) {
        s_entries[idx].value = value;
        return;
    }
    if (s_count >= kMaxSignals) return;   // молча игнорируем переполнение

    strncpy(s_entries[s_count].id, id, kMaxIdLen - 1);
    s_entries[s_count].id[kMaxIdLen - 1] = '\0';
    s_entries[s_count].value = value;
    ++s_count;
}

float get(const char* id, float fallback) {
    if (!id || !*id) return fallback;
    const int idx = find(id);
    return idx >= 0 ? s_entries[idx].value : fallback;
}

bool has(const char* id) {
    return id && *id && find(id) >= 0;
}

void clear() { s_count = 0; }

uint8_t count() { return s_count; }

} // namespace Signals
