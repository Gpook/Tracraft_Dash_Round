#include "qspi_dma.h"

#if defined(ESP32) && CONFIG_IDF_TARGET_ESP32S3

#include <esp_heap_caps.h>
#include <string.h>

namespace {

/// Кусок передачи в байтах — им же ограничен пул DMA-дескрипторов.
constexpr int kMaxTransferSz =
    static_cast<int>(Arduino_ESP32QSPI_DMA::kChunkPixels) * 2 + 64;

/**
 * Перестановка байт в паре пикселей.
 *
 * Панель принимает RGB565 старшим байтом вперёд, фреймбуфер хранит пиксели в
 * родном для ESP32 порядке — младшим вперёд. Значит перед отправкой байты
 * внутри каждого пикселя надо поменять местами.
 *
 * Слово содержит два пикселя: [a_lo a_hi b_lo b_hi] -> [a_hi a_lo b_hi b_lo].
 * Четыре операции ALU на два пикселя, без ветвлений и без обращений к памяти
 * кроме одного чтения и одной записи.
 */
inline uint32_t swapPair(uint32_t two) {
    return ((two & 0x00FF00FFu) << 8) | ((two >> 8) & 0x00FF00FFu);
}

/**
 * Подготовка куска к отправке.
 *
 * Библиотечный вариант читал по одному пикселю: два 16-битных обращения к
 * PSRAM на каждое слово результата. PSRAM на этом пути узкое место, и число
 * обращений к ней определяет время подготовки — поэтому здесь читается сразу
 * 32-битное слово, то есть два пикселя за одно обращение.
 *
 * Цикл развёрнут по четыре слова: контроллер PSRAM отдаёт данные пачками, и
 * на развёрнутом теле успевает подтянуть следующую пачку, пока ALU занят
 * перестановкой предыдущей.
 */
void fillSwapped(uint32_t* dst, const uint16_t* src, uint32_t pixels) {
    // 32-битное чтение требует выравнивания. Фреймбуфер выровнен на 16 байт,
    // а полосы кадра начинаются с чётных строк по 466 px (=932 байта, кратно
    // четырём), так что обычный путь всегда выровнен. Проверка нужна для
    // остальных вызывающих: попасть сюда с нечётного пикселя можно, и тогда
    // 32-битное чтение дало бы исключение.
    if ((reinterpret_cast<uintptr_t>(src) & 3u) != 0) {
        for (uint32_t i = 0; i + 1 < pixels; i += 2) {
            const uint32_t a = src[i], b = src[i + 1];
            dst[i >> 1] = static_cast<uint32_t>(((b & 0xFF00u) << 8) | ((b & 0xFFu) << 24) |
                                                ((a & 0xFF00u) >> 8) | ((a & 0xFFu) << 8));
        }
        if (pixels & 1) {
            const uint16_t p = src[pixels - 1];
            reinterpret_cast<uint16_t*>(dst)[pixels - 1] =
                static_cast<uint16_t>((p >> 8) | (p << 8));
        }
        return;
    }

    const uint32_t  words = pixels >> 1;
    const uint32_t* s     = reinterpret_cast<const uint32_t*>(src);

    uint32_t i = 0;
    for (; i + 4 <= words; i += 4) {
        const uint32_t w0 = s[i], w1 = s[i + 1], w2 = s[i + 2], w3 = s[i + 3];
        dst[i]     = swapPair(w0);
        dst[i + 1] = swapPair(w1);
        dst[i + 2] = swapPair(w2);
        dst[i + 3] = swapPair(w3);
    }
    for (; i < words; ++i) dst[i] = swapPair(s[i]);

    if (pixels & 1) {
        const uint16_t p = src[pixels - 1];
        reinterpret_cast<uint16_t*>(dst)[pixels - 1] =
            static_cast<uint16_t>((p >> 8) | (p << 8));
    }
}

} // namespace

Arduino_ESP32QSPI_DMA::Arduino_ESP32QSPI_DMA(int8_t cs, int8_t sck, int8_t d0,
                                             int8_t d1, int8_t d2, int8_t d3,
                                             bool shared)
    : _cs(cs), _sck(sck), _d0(d0), _d1(d1), _d2(d2), _d3(d3), _shared(shared) {}

bool Arduino_ESP32QSPI_DMA::begin(int32_t speed, int8_t dataMode) {
    _speed    = (speed <= GFX_NOT_DEFINED) ? 40000000 : speed;
    _dataMode = (dataMode == GFX_NOT_DEFINED) ? SPI_MODE0 : dataMode;

    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    // CS дёргаем сами через регистры GPIO, а не средствами драйвера SPI:
    // сигнал обязан остаться низким на все куски одного кадра, иначе панель
    // воспримет каждый кусок как новый поток пикселей.
    _csPinMask = digitalPinToBitMask(_cs);
    if (_cs >= 32) {
        _csPortSet = (PORTreg_t)GPIO_OUT1_W1TS_REG;
        _csPortClr = (PORTreg_t)GPIO_OUT1_W1TC_REG;
    } else {
        _csPortSet = (PORTreg_t)GPIO_OUT_W1TS_REG;
        _csPortClr = (PORTreg_t)GPIO_OUT_W1TC_REG;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num     = _d0,
        .miso_io_num     = _d1,
        .sclk_io_num     = _sck,
        .quadwp_io_num   = _d2,
        .quadhd_io_num   = _d3,
        .data4_io_num    = -1,
        .data5_io_num    = -1,
        .data6_io_num    = -1,
        .data7_io_num    = -1,
        .max_transfer_sz = kMaxTransferSz,
        .flags           = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .isr_cpu_id      = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags      = 0,
    };
    if (spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    spi_device_interface_config_t devcfg = {
        .command_bits     = 8,
        .address_bits     = 24,
        .dummy_bits       = 0,
        .mode             = static_cast<uint8_t>(_dataMode),
        .clock_source     = SPI_CLK_SRC_DEFAULT,
        .duty_cycle_pos   = 0,
        .cs_ena_pretrans  = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz   = _speed,
        .input_delay_ns   = 0,
        .spics_io_num     = -1,   // CS вручную, см. выше
        .flags            = SPI_DEVICE_HALFDUPLEX,
        .queue_size       = 1,
        .pre_cb           = nullptr,
        .post_cb          = nullptr,
    };
    if (spi_bus_add_device(SPI2_HOST, &devcfg, &_handle) != ESP_OK) return false;

    // Опрашиваемые транзакции (spi_device_polling_*) требуют захваченной шины.
    if (!_shared) spi_device_acquire_bus(_handle, portMAX_DELAY);

    memset(&_tranExt, 0, sizeof(_tranExt));
    _tran = reinterpret_cast<spi_transaction_t*>(&_tranExt);

    for (int i = 0; i < 2; ++i) {
        _buf[i] = static_cast<uint32_t*>(
            heap_caps_aligned_alloc(16, kChunkPixels * 2, MALLOC_CAP_DMA));
        if (!_buf[i]) return false;
    }
    return true;
}

void Arduino_ESP32QSPI_DMA::beginWrite() {
    if (_shared) spi_device_acquire_bus(_handle, portMAX_DELAY);
}

void Arduino_ESP32QSPI_DMA::endWrite() {
    if (_shared) spi_device_release_bus(_handle);
}

// ─── Команды ────────────────────────────────────────────────────────────────
//
// Опкоды и флаги воспроизведены за Arduino_ESP32QSPI без изменений: 0x02 —
// команда с адресом по одной линии, 0x32 — поток данных по четырём. Панель
// CO5300 инициализируется этой последовательностью, отклоняться нельзя.

void Arduino_ESP32QSPI_DMA::writeCommand(uint8_t c) {
    csLow();
    _tranExt.base.flags     = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tranExt.base.cmd       = 0x02;
    _tranExt.base.addr      = static_cast<uint32_t>(c) << 8;
    _tranExt.base.tx_buffer = nullptr;
    _tranExt.base.length    = 0;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::writeCommand16(uint16_t c) {
    csLow();
    _tranExt.base.flags     = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tranExt.base.cmd       = 0x02;
    _tranExt.base.addr      = c;
    _tranExt.base.tx_buffer = nullptr;
    _tranExt.base.length    = 0;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::writeCommandBytes(uint8_t* data, uint32_t len) {
    csLow();
    while (len) {
        const uint32_t l = (len >= (kChunkPixels << 1)) ? (kChunkPixels << 1) : len;
        _tranExt.base.flags     = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
        _tranExt.base.tx_buffer = data;
        _tranExt.base.length    = l << 3;
        pollStart();
        pollEnd();
        len -= l;
        data += l;
    }
    csHigh();
}

void Arduino_ESP32QSPI_DMA::write(uint8_t d) {
    csLow();
    _tranExt.base.flags      = SPI_TRANS_USE_TXDATA | SPI_TRANS_MODE_QIO;
    _tranExt.base.cmd        = 0x32;
    _tranExt.base.addr       = 0x003C00;
    _tranExt.base.tx_data[0] = d;
    _tranExt.base.length     = 8;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::write16(uint16_t d) {
    csLow();
    _tranExt.base.flags      = SPI_TRANS_USE_TXDATA | SPI_TRANS_MODE_QIO;
    _tranExt.base.cmd        = 0x32;
    _tranExt.base.addr       = 0x003C00;
    _tranExt.base.tx_data[0] = d >> 8;
    _tranExt.base.tx_data[1] = d;
    _tranExt.base.length     = 16;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::writeC8D8(uint8_t c, uint8_t d) {
    csLow();
    _tranExt.base.flags      = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD |
                               SPI_TRANS_MULTILINE_ADDR;
    _tranExt.base.cmd        = 0x02;
    _tranExt.base.addr       = static_cast<uint32_t>(c) << 8;
    _tranExt.base.tx_data[0] = d;
    _tranExt.base.length     = 8;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::writeC8D16(uint8_t c, uint16_t d) {
    csLow();
    _tranExt.base.flags      = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD |
                               SPI_TRANS_MULTILINE_ADDR;
    _tranExt.base.cmd        = 0x02;
    _tranExt.base.addr       = static_cast<uint32_t>(c) << 8;
    _tranExt.base.tx_data[0] = d >> 8;
    _tranExt.base.tx_data[1] = d;
    _tranExt.base.length     = 16;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::writeC8D16D16(uint8_t c, uint16_t d1, uint16_t d2) {
    csLow();
    _tranExt.base.flags      = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD |
                               SPI_TRANS_MULTILINE_ADDR;
    _tranExt.base.cmd        = 0x02;
    _tranExt.base.addr       = static_cast<uint32_t>(c) << 8;
    _tranExt.base.tx_data[0] = d1 >> 8;
    _tranExt.base.tx_data[1] = d1;
    _tranExt.base.tx_data[2] = d2 >> 8;
    _tranExt.base.tx_data[3] = d2;
    _tranExt.base.length     = 32;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::writeC8D16D16Split(uint8_t c, uint16_t d1, uint16_t d2) {
    writeC8D16D16(c, d1, d2);
}

void Arduino_ESP32QSPI_DMA::writeC8Bytes(uint8_t c, uint8_t* data, uint32_t len) {
    csLow();
    _tranExt.base.flags     = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _tranExt.base.cmd       = 0x02;
    _tranExt.base.addr      = static_cast<uint32_t>(c) << 8;
    _tranExt.base.tx_buffer = data;
    _tranExt.base.length    = len << 3;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI_DMA::batchOperation(const uint8_t* operations, size_t len) {
    // Копия реализации Arduino_ESP32QSPI. Отличие от базовой в том, что пары
    // «команда + параметр» уходят одной транзакцией через writeC8D8/D16, как
    // требует протокол панели.
    auto* scratch = reinterpret_cast<uint8_t*>(_buf[0]);

    for (size_t i = 0; i < len; ++i) {
        uint8_t l = 0;
        switch (operations[i]) {
        case BEGIN_WRITE:
            beginWrite();
            break;
        case WRITE_COMMAND_8:
            writeCommand(operations[++i]);
            break;
        case WRITE_COMMAND_16: {
            const uint8_t msb = operations[++i];
            const uint8_t lsb = operations[++i];
            writeCommand16(static_cast<uint16_t>((msb << 8) | lsb));
            break;
        }
        case WRITE_DATA_8:
            write(operations[++i]);
            break;
        case WRITE_DATA_16: {
            const uint8_t msb = operations[++i];
            const uint8_t lsb = operations[++i];
            write16(static_cast<uint16_t>((msb << 8) | lsb));
            break;
        }
        case WRITE_BYTES:
            // Список операций лежит во флеше, а DMA читать флеш не умеет,
            // поэтому байты сначала переносятся в DMA-память.
            l = operations[++i];
            memcpy(scratch, operations + i + 1, l);
            i += l;
            writeBytes(scratch, l);
            break;
        case WRITE_C8_D8:
            l = operations[++i];
            writeC8D8(l, operations[++i]);
            break;
        case WRITE_C8_D16: {
            const uint8_t c   = operations[++i];
            const uint8_t msb = operations[++i];
            const uint8_t lsb = operations[++i];
            writeC8D16(c, static_cast<uint16_t>((msb << 8) | lsb));
            break;
        }
        case WRITE_C8_BYTES: {
            const uint8_t c = operations[++i];
            l               = operations[++i];
            memcpy(scratch, operations + i + 1, l);
            i += l;
            writeC8Bytes(c, scratch, l);
            break;
        }
        case WRITE_C16_D16:
            break;
        case END_WRITE:
            endWrite();
            break;
        case DELAY:
            delay(operations[++i]);
            break;
        default:
            Serial.printf("[qspi] неизвестная операция %u на позиции %u\n",
                          operations[i], static_cast<unsigned>(i));
            break;
        }
    }
}

void Arduino_ESP32QSPI_DMA::writeRepeat(uint16_t p, uint32_t len) {
    // Один цвет: буфер заполняется один раз и переиспользуется, чередование
    // тут не нужно — подготовки на каждый кусок нет.
    const uint32_t bufPixels = (len >= kChunkPixels) ? kChunkPixels : len;
    const uint32_t pair      = static_cast<uint32_t>(((p & 0xFF00u) << 8) |
                                                     ((p & 0xFFu) << 24) |
                                                     ((p & 0xFF00u) >> 8) |
                                                     ((p & 0xFFu) << 8));
    for (uint32_t i = 0; i < (bufPixels + 1) / 2; ++i) _buf[0][i] = pair;

    csLow();
    bool first = true;
    while (len) {
        const uint32_t l = (bufPixels <= len) ? bufPixels : len;
        if (first) {
            _tranExt.base.flags = SPI_TRANS_MODE_QIO;
            _tranExt.base.cmd   = 0x32;
            _tranExt.base.addr  = 0x003C00;
            first               = false;
        } else {
            _tranExt.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                                  SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        _tranExt.base.tx_buffer = _buf[0];
        _tranExt.base.length    = l << 4;
        pollStart();
        pollEnd();
        len -= l;
    }
    csHigh();
}

void Arduino_ESP32QSPI_DMA::writeBytes(uint8_t* data, uint32_t len) {
    // Данные уже в нужном порядке — отправляем напрямую, без промежуточного
    // буфера и без конвейера: перекладывать нечего.
    csLow();
    bool first = true;
    while (len) {
        const uint32_t l = (len >= (kChunkPixels << 1)) ? (kChunkPixels << 1) : len;
        if (first) {
            _tranExt.base.flags = SPI_TRANS_MODE_QIO;
            _tranExt.base.cmd   = 0x32;
            _tranExt.base.addr  = 0x003C00;
            first               = false;
        } else {
            _tranExt.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                                  SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        _tranExt.base.tx_buffer = data;
        _tranExt.base.length    = l << 3;
        pollStart();
        pollEnd();
        len -= l;
        data += l;
    }
    csHigh();
}

/**
 * Отправка пикселей кадра — конвейер.
 *
 * Порядок операций тут и есть весь смысл класса. В библиотечной версии на
 * каждый кусок шло: заполнить буфер, запустить передачу, дождаться её конца.
 * Процессор и шина работали по очереди, и кадр стоил сумму их времён.
 *
 * Здесь ожидание предыдущей передачи сдвинуто ПОСЛЕ заполнения следующего
 * буфера. Пока DMA читает буфер A, процессор готовит буфер B; ждать приходится
 * только разницу, а не полное время передачи. Кадр стоит max(подготовка,
 * передача).
 *
 * Транзакция _tranExt при этом никогда не меняется во время активной передачи:
 * поля правятся строго между pollEnd() и pollStart(). Меняется только тот
 * буфер, который в этот момент не читает DMA, — за это отвечает чередование
 * slot.
 */
void Arduino_ESP32QSPI_DMA::writePixels(uint16_t* data, uint32_t len) {
    csLow();

    bool    first    = true;   // первая транзакция задаёт команду и адрес
    bool    inFlight = false;  // есть незавершённая передача
    uint8_t slot     = 0;      // какой буфер заполняем

    while (len) {
        const uint32_t l   = (len > kChunkPixels) ? kChunkPixels : len;
        uint32_t*      buf = _buf[slot];

        // Заполняем свободный буфер. Здесь и происходит совмещение: DMA в это
        // время ещё отдаёт предыдущий кусок.
        fillSwapped(buf, data, l);

        // И только теперь ждём шину.
        if (inFlight) {
            const uint32_t t0 = micros();
            pollEnd();
            _waitedUs += micros() - t0;
            inFlight = false;
        }

        if (first) {
            _tranExt.base.flags = SPI_TRANS_MODE_QIO;
            _tranExt.base.cmd   = 0x32;
            _tranExt.base.addr  = 0x003C00;
            first               = false;
        } else {
            // Продолжение того же потока пикселей: команда и адрес не нужны,
            // переменные поля обнулены в begin().
            _tranExt.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                                  SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        _tranExt.base.tx_buffer = buf;
        _tranExt.base.length    = l << 4;

        pollStart();
        inFlight = true;

        data += l;
        len -= l;
        slot ^= 1;
    }

    if (inFlight) {
        const uint32_t t0 = micros();
        pollEnd();
        _waitedUs += micros() - t0;
    }
    csHigh();
}

#endif // ESP32S3
