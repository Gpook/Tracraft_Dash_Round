/**
 * @file  qspi_dma.h
 * @brief Шина QSPI с конвейером: заполнение буфера параллельно с передачей.
 *
 * Замена Arduino_ESP32QSPI из GFX. Протокол панели воспроизведён байт в байт
 * (те же опкоды 0x02/0x32, те же флаги QIO, тот же ручной CS), отличается
 * только отправка пикселей.
 *
 * Зачем свой класс. В библиотечном writePixels заполнение DMA-буфера и его
 * передача строго последовательны: пока процессор перекладывает пиксели из
 * PSRAM, шина простаивает, а пока идёт передача — простаивает процессор.
 * Замер на 466x466 давал 43 мс на кадр при теоретических 11 мс, то есть три
 * четверти времени железо ждало само себя. Два DMA-буфера библиотека при этом
 * выделяет (и использует в writeYCbCrPixels), но в writePixels второй не
 * задействован.
 *
 * Здесь буферы чередуются: процессор готовит следующий кусок, пока DMA
 * отдаёт предыдущий, и кадр стоит max(подготовка, передача) вместо их суммы.
 * Плюс подготовка сама переписана — см. writePixels в qspi_dma.cpp.
 *
 * Класс живёт в проекте, а не патчем в .pio/libdeps, чтобы правка не
 * исчезала при переустановке зависимостей.
 */

#pragma once

#include <Arduino_DataBus.h>

#if defined(ESP32) && CONFIG_IDF_TARGET_ESP32S3

#include <driver/spi_master.h>

class Arduino_ESP32QSPI_DMA : public Arduino_DataBus {
public:
    /// Пикселей в одном куске передачи. 8192 px = 16 КБ на буфер, два буфера
    /// в DMA-памяти = 32 КБ. Крупнее — меньше накладных расходов на
    /// транзакцию, но хуже заполняется конвейер: первая подготовка и
    /// последняя передача ни с чем не совмещаются.
    static constexpr uint32_t kChunkPixels = 8192;

    Arduino_ESP32QSPI_DMA(int8_t cs, int8_t sck, int8_t d0, int8_t d1,
                          int8_t d2, int8_t d3, bool shared = false);

    bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t dataMode = GFX_NOT_DEFINED) override;
    void beginWrite() override;
    void endWrite() override;

    void writeCommand(uint8_t c) override;
    void writeCommand16(uint16_t c) override;
    void writeCommandBytes(uint8_t* data, uint32_t len) override;
    void write(uint8_t d) override;
    void write16(uint16_t d) override;

    void writeC8D8(uint8_t c, uint8_t d) override;
    void writeC8D16(uint8_t c, uint16_t d) override;
    void writeC8D16D16(uint8_t c, uint16_t d1, uint16_t d2) override;
    void writeC8D16D16Split(uint8_t c, uint16_t d1, uint16_t d2) override;
    void writeC8Bytes(uint8_t c, uint8_t* data, uint32_t len);

    void writeRepeat(uint16_t p, uint32_t len) override;
    void writeBytes(uint8_t* data, uint32_t len) override;

    /// Обязателен к переопределению: базовая реализация разбивает WRITE_C8_D8
    /// на writeCommand() + write(), то есть на две транзакции, причём вторая
    /// уходит опкодом потока пикселей 0x32. Инициализация CO5300 состоит
    /// почти целиком из таких пар и с базовым вариантом не проходит.
    void batchOperation(const uint8_t* operations, size_t len) override;

    /// Горячий путь: отправка пикселей кадра. Конвейер живёт здесь.
    void writePixels(uint16_t* data, uint32_t len) override;

    /// Суммарное время ожидания шины за последний сброс, мкс. Диагностика:
    /// показывает, упирается кадр в передачу или в подготовку данных.
    uint32_t waitedUs() const { return _waitedUs; }
    void     resetWaited() { _waitedUs = 0; }

private:
    inline void csHigh() { *_csPortSet = _csPinMask; }
    inline void csLow() { *_csPortClr = _csPinMask; }
    inline void pollStart() { spi_device_polling_start(_handle, _tran, portMAX_DELAY); }
    inline void pollEnd() { spi_device_polling_end(_handle, portMAX_DELAY); }

    int8_t _cs, _sck, _d0, _d1, _d2, _d3;
    bool   _shared;

    PORTreg_t _csPortSet = nullptr;
    PORTreg_t _csPortClr = nullptr;
    uint32_t  _csPinMask = 0;

    spi_device_handle_t   _handle = nullptr;
    spi_transaction_ext_t _tranExt{};
    spi_transaction_t*    _tran = nullptr;

    /// Два DMA-буфера под чередование. Оба во внутренней памяти: DMA к PSRAM
    /// на S3 работает, но с меньшей пропускной способностью.
    uint32_t* _buf[2] = {nullptr, nullptr};

    uint32_t _waitedUs = 0;
};

#endif // ESP32S3
