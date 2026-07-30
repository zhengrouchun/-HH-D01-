/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file SPI.cpp
 * @brief SPIClass implementation for SPI communication - Full SDK integration
 * @version 3.0
 * @date 2026-04-29
 */

#include "SPI.h"
#include "Arduino.h"
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
#include "pinctrl.h"
#endif

#define SPI_INVALID_CS_PIN 255
#define SPI_TRANSFER_TIMEOUT_MS 100
#define SPI_BUFFER_TRANSFER_TIMEOUT_MS 1000
#define SPI_BASE_CLOCK_FREQ ARDUINO_SPI_BUS_CLK_HZ
#define SPI_BYTE_SHIFT 8

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
// SPI master pin assignment (CONFIG_SPI_DI/DO/CLK/CS_MASTER_PIN,
// CONFIG_SPI_MASTER_PIN_MODE) comes from the chip porting layer's
// arduino_config.h (included via Arduino.h above).

// Arduino SPI abstraction is single-master-single-slave; always select slave 0.
// If multi-slave support is needed in the future, convert this to a Kconfig item.
#define SPI_MASTER_SLAVE_NUM 1
#endif

// LSBFIRST support strategy:
//   - If the chip SPI hardware supports LSB/MSB selection, the chip porting
//     layer defines ARDUINO_SPI_HW_LSB_FIRST; setBitOrder() then configures the
//     hardware and the transfer functions skip software bit reversal.
//   - Otherwise (the common case — most SPI IPs only do MSB) LSBFIRST is
//     emulated by reversing the bit order of each byte in software.
#ifdef ARDUINO_SPI_HW_LSB_FIRST
#define SPI_NEED_SW_BITREV 0
#else
#define SPI_NEED_SW_BITREV 1
#endif

// Helper: reverse bit order for LSBFIRST emulation (used when !SPI_NEED_SW_BITREV
// is false — i.e. hardware does not support bit-order selection).
static inline uint8_t bit_reverse_uint8(uint8_t data)
{
    // 0xAA = 10101010b, swap odd/even bits: (data & 10101010) >> 1 | (data & 01010101) << 1
    data = ((data & 0xAA) >> 1) | ((data & 0x55) << 1); /* 1 = swap adjacent bits, 0x55 = 01010101b */
    data = ((data & 0xCC) >> 2) | ((data & 0x33) << 2); /* 2 = swap bit pairs, 0x33 = 00110011b */
    data = (data >> 4) | (data << 4);                   /* 4 = swap nibbles across whole byte */
    return data;
}

static inline uint16_t bit_reverse_uint16(uint16_t data)
{
    // 0xAAAA = 1010101010101010b, swap odd/even bits
    data = ((data & 0xAAAA) >> 1) | ((data & 0x5555) << 1); /* 1 = swap adjacent bits, 0x5555 = 01010101...b */
    data = ((data & 0xCCCC) >> 2) | ((data & 0x3333) << 2); /* 2 = swap bit pairs, 0x3333 = 00110011...b */
    data = ((data & 0xF0F0) >> 4) | ((data & 0x0F0F) << 4); /* 4 = swap nibbles, 0xF0F0 = 11110000...b */
    data = (data >> 8) | (data << 8);                       /* 8 = swap high/low bytes */
    return data;
}

// SPIClass Constructor
SPIClass::SPIClass()
    : _initialized(false),
      _in_transaction(false),
      _clock_freq(SPI_DEFAULT_CLOCK),
      _bit_order(MSBFIRST),
      _data_mode(SPI_MODE0),
      _cs_pin(SPI_INVALID_CS_PIN), // Invalid pin
      _using_interrupt(false),
      _interrupt_num(0)
{
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    // Initialize selected slave to default
    _selected_slave = SPI_SLAVE0;
#endif
}

// SPIClass Destructor
SPIClass::~SPIClass()
{
    end();
}

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
// Populate a master-mode spi_attr_t from the current configuration. Every field
// must be set explicitly: uapi_spi_init() validates frame_size and uses bus_clk
// for the baud-rate divider, so a zero-initialised attr (missing frame_size /
// bus_clk) makes init fail.
void SPIClass::fillMasterAttr(spi_attr_t *attr) const
{
    memset(attr, 0, sizeof(*attr));

    attr->freq_mhz = _clock_freq / 1000000; // 1000000 used to convert Hz to MHz
    switch (_data_mode) {
        case SPI_MODE1:
            attr->clk_polarity = 0;
            attr->clk_phase = 1;
            break;
        case SPI_MODE2:
            attr->clk_polarity = 1;
            attr->clk_phase = 0;
            break;
        case SPI_MODE3:
            attr->clk_polarity = 1;
            attr->clk_phase = 1;
            break;
        case SPI_MODE0:
        default:
            attr->clk_polarity = 0;
            attr->clk_phase = 0;
            break;
    }

    attr->is_slave = false;
    attr->slave_num = SPI_MASTER_SLAVE_NUM;
    attr->bus_clk = SPI_BASE_CLOCK_FREQ;
    attr->frame_format = 0;
    attr->spi_frame_format = HAL_SPI_FRAME_FORMAT_STANDARD;
    attr->frame_size = HAL_SPI_FRAME_SIZE_8;
    attr->tmod = 0;
    attr->sste = 0;
    // Bit order is not set here: most SPI IPs (incl. the current chips) have no
    // LSB/MSB register field, so LSBFIRST is emulated in software (see top of
    // file). A chip with hardware LSB support defines ARDUINO_SPI_HW_LSB_FIRST
    // and setBitOrder() configures it then.
}
#endif

// Initialize SPI
void SPIClass::begin()
{
    begin(SPI_INVALID_CS_PIN); // No CS pin by default
}

// Initialize SPI with CS pin
void SPIClass::begin(uint8_t cs_pin)
{
    if (_initialized) {
        end();
    }

    _cs_pin = cs_pin;

    // Configure CS pin as output and set HIGH (inactive)
    if (_cs_pin != SPI_INVALID_CS_PIN) {
        pinMode(_cs_pin, OUTPUT);
        digitalWrite(_cs_pin, HIGH);
    }

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    // Configure SPI pinmux (CLK/DI/DO/CS) before init. Without this the pads
    // stay in their default GPIO function and no signal reaches the bus.
    uapi_pin_set_mode((pin_t)CONFIG_SPI_DI_MASTER_PIN, (pin_mode_t)CONFIG_SPI_MASTER_PIN_MODE);
    uapi_pin_set_mode((pin_t)CONFIG_SPI_DO_MASTER_PIN, (pin_mode_t)CONFIG_SPI_MASTER_PIN_MODE);
    uapi_pin_set_mode((pin_t)CONFIG_SPI_CLK_MASTER_PIN, (pin_mode_t)CONFIG_SPI_MASTER_PIN_MODE);
    uapi_pin_set_mode((pin_t)CONFIG_SPI_CS_MASTER_PIN, (pin_mode_t)CONFIG_SPI_MASTER_PIN_MODE);

    // Configure SPI attributes
    spi_attr_t attr;
    fillMasterAttr(&attr);

    // Extra attributes
    spi_extra_attr_t extra_attr;
    memset(&extra_attr, 0, sizeof(extra_attr));

    // Initialize SPI as master
    errcode_t ret = uapi_spi_init(SPI_BUS_0, &attr, &extra_attr);
    if (ret == ERRCODE_SUCC) {
        _initialized = true;
    }
#else
    // Stub: Mark as initialized even without SDK support
    // This allows Arduino sketches to compile and run without hardware
    _initialized = true;
#endif
}

// End SPI
void SPIClass::end()
{
    if (!_initialized) {
        return;
    }

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    uapi_spi_deinit(SPI_BUS_0);
#endif

    _initialized = false;
    _in_transaction = false;
}

// Begin SPI transaction
void SPIClass::beginTransaction(SPISettings settings)
{
    // Validate SPI settings
    if (settings._clock == 0 || settings._clock > SPI_MAX_CLOCK) {
        return;
    }
    if (settings._bitOrder != LSBFIRST && settings._bitOrder != MSBFIRST) {
        return;
    }
    if (settings._dataMode != SPI_MODE0 && settings._dataMode != SPI_MODE1 && settings._dataMode != SPI_MODE2 &&
        settings._dataMode != SPI_MODE3) {
        return;
    }
    if (!_initialized) {
        return;
    }
    if (_in_transaction) {
        endTransaction();
    }

    // Save current settings for potential restore
    _saved_clock = _clock_freq;
    _saved_bit_order = _bit_order;
    _saved_data_mode = _data_mode;

    // Apply new settings
    setClock(settings._clock);
    setBitOrder(settings._bitOrder);
    setDataMode(settings._dataMode);

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    // Apply new settings in place instead of tearing down the bus on every transaction.
    spi_attr_t attr;
    fillMasterAttr(&attr);

    errcode_t attr_ret = uapi_spi_set_attr(SPI_BUS_0, &attr);
    if (attr_ret != ERRCODE_SUCC) {
        _initialized = false;
    }
#endif

    _in_transaction = true;

    // Assert CS pin (active low) if configured
    if (_cs_pin != SPI_INVALID_CS_PIN) {
        digitalWrite(_cs_pin, LOW);
    }

    // Disable interrupts if configured
    if (_using_interrupt) {
        noInterrupts();
    }
}

// End SPI transaction
void SPIClass::endTransaction()
{
    if (!_in_transaction) {
        return;
    }

    // Deassert CS pin (active low) if configured
    if (_cs_pin != SPI_INVALID_CS_PIN) {
        digitalWrite(_cs_pin, HIGH);
    }

    // Re-enable interrupts if they were disabled
    if (_using_interrupt) {
        interrupts();
    }

    // Restore settings saved at beginTransaction()
    setClock(_saved_clock);
    setBitOrder(_saved_bit_order);
    setDataMode(_saved_data_mode);

    _in_transaction = false;
}

// Transfer a byte
uint8_t SPIClass::transfer(uint8_t data)
{
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    if (!_initialized) {
        return 0; // Not initialized, return 0 to indicate error (not loopback)
    }

    // Emulate LSBFIRST via software bit reversal when the hardware is MSB-only.
    if (SPI_NEED_SW_BITREV && _bit_order == LSBFIRST) {
        data = bit_reverse_uint8(data);
    }

    spi_xfer_data_t xfer;
    uint8_t rx_data = 0;

    xfer.tx_buff = &data;
    xfer.tx_bytes = 1;
    xfer.rx_buff = &rx_data;
    xfer.rx_bytes = 1;

    errcode_t ret = uapi_spi_master_writeread(SPI_BUS_0, &xfer, SPI_TRANSFER_TIMEOUT_MS);
    if (ret == ERRCODE_SUCC) {
        if (SPI_NEED_SW_BITREV && _bit_order == LSBFIRST) {
            rx_data = bit_reverse_uint8(rx_data);
        }
        return rx_data;
    }

    // Transfer failed - return 0 to indicate error
    return 0;
#else
    // Stub: Return input data (loopback) for compatibility
    return data;
#endif
}

// Transfer 16-bit data
uint16_t SPIClass::transfer16(uint16_t data)
{
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    if (!_initialized) {
        return 0; // Not initialized, return 0 to indicate error (not loopback)
    }

    // Transfer high byte first (MSBFIRST) or low byte first (LSBFIRST)
    uint8_t high;
    uint8_t low;
    if (_bit_order == MSBFIRST) {
        high = transfer((data >> SPI_BYTE_SHIFT) & 0xFF);
        low = transfer(data & 0xFF);
    } else {
        low = transfer((data >> SPI_BYTE_SHIFT) & 0xFF);
        high = transfer(data & 0xFF);
    }
    return ((uint16_t)high << SPI_BYTE_SHIFT) | low;
#else
    // Stub: Return input data for compatibility
    return data;
#endif
}

// Transfer buffer
void SPIClass::transfer(void *buf, size_t count)
{
    if (buf == nullptr || count == 0) {
        return;
    }

    uint8_t *buffer = (uint8_t *)buf;

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    if (!_initialized) {
        return; // Not initialized, no-op
    }

    // Emulate LSBFIRST via software bit reversal when the hardware is MSB-only.
    // In-place: tx_buff == rx_buff, so the reversal of received bytes is a no-op
    // (they come back already reversed relative to what was sent). When the
    // hardware supports LSB (SPI_NEED_SW_BITREV == 0), skip this entirely.
    if (SPI_NEED_SW_BITREV && _bit_order == LSBFIRST) {
        for (size_t i = 0; i < count; i++) {
            buffer[i] = bit_reverse_uint8(buffer[i]);
        }
    }

    spi_xfer_data_t xfer;
    xfer.tx_buff = buffer;
    xfer.tx_bytes = (uint32_t)count;
    xfer.rx_buff = buffer; // NOTE: In-place transfer — rx overwrites tx buffer.
                           // Caller must not rely on tx data after this call.
    xfer.rx_bytes = (uint32_t)count;

    uapi_spi_master_writeread(SPI_BUS_0, &xfer, SPI_BUFFER_TRANSFER_TIMEOUT_MS);

    // For LSBFIRST with in-place transfer, received bytes land in the same
    // buffer already bit-reversed (relative to the MSB the hardware clocked in),
    // so no post-reversal is needed.
#else
    // Stub: No-op for buffer transfer
    // Data in buffer remains unchanged (no loopback for buffer)
    (void)buffer;
#endif
}

// Set bit order
void SPIClass::setBitOrder(uint8_t bitOrder)
{
    _bit_order = bitOrder;
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1) && SPI_NEED_SW_BITREV == 0
    // Hardware supports LSB/MSB selection (ARDUINO_SPI_HW_LSB_FIRST): apply it.
    if (_initialized && !_in_transaction) {
        spi_attr_t attr;
        memset(&attr, 0, sizeof(attr));
        uapi_spi_get_attr(SPI_BUS_0, &attr);
        attr.spi_frame_format = (bitOrder == LSBFIRST) ? 1 : 0;  // 1=LSB, 0=MSB
        uapi_spi_set_attr(SPI_BUS_0, &attr);
    }
#endif
    // When SPI_NEED_SW_BITREV (hardware is MSB-only), no hardware reconfiguration
    // is needed — LSBFIRST is emulated by bit reversal in the transfer functions.
}

// Set data mode
void SPIClass::setDataMode(uint8_t mode)
{
    _data_mode = mode;

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    if (_initialized && !_in_transaction) {
        // Apply immediately if not in a transaction
        spi_attr_t attr;
        memset(&attr, 0, sizeof(attr));

        // Get current attributes first
        uapi_spi_get_attr(SPI_BUS_0, &attr);

        // Update data mode - SPI_MODE values: bit2=CPOL, bit1=CPHA
        switch (_data_mode) {
            case SPI_MODE1:
                attr.clk_polarity = 0;
                attr.clk_phase = 1;
                break;
            case SPI_MODE2:
                attr.clk_polarity = 1;
                attr.clk_phase = 0;
                break;
            case SPI_MODE3:
                attr.clk_polarity = 1;
                attr.clk_phase = 1;
                break;
            case SPI_MODE0:
            default:
                attr.clk_polarity = 0;
                attr.clk_phase = 0;
                break;
        }

        errcode_t ret2 = uapi_spi_set_attr(SPI_BUS_0, &attr);
        if (ret2 != ERRCODE_SUCC) {
            // Failed to apply data mode change
            return;
        }
    }
#endif
}

// Set clock divider
void SPIClass::setClockDivider(uint8_t div)
{
    // Calculate frequency based on divider.
    // Base clock is the chip SSI source clock (ARDUINO_SPI_BUS_CLK_HZ, from the
    // chip porting layer's arduino_config.h — value is chip-specific).
    uint32_t base_clock = SPI_BASE_CLOCK_FREQ;
    if (div == 0) {
        div = 1; // Prevent division by zero
    }
    uint32_t freq = base_clock / div;
    setClock(freq);
}

// Set clock frequency
void SPIClass::setClock(uint32_t clockFreq)
{
    _clock_freq = clockFreq;

#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    if (_initialized && !_in_transaction) {
        // Update frequency via SDK
        spi_attr_t attr;
        memset(&attr, 0, sizeof(attr));

        // Get current attributes first
        uapi_spi_get_attr(SPI_BUS_0, &attr);

        // freq_mhz is in MHz, convert from Hz
        attr.freq_mhz = _clock_freq / 1000000; // 1000000 used to convert Hz to MHz
        uapi_spi_set_attr(SPI_BUS_0, &attr);
    }
#endif
}

// Set frequency (alias for setClock)
void SPIClass::setFrequency(uint32_t freq)
{
    setClock(freq);
}

// Configure interrupt protection
void SPIClass::usingInterrupt(uint8_t interruptNum)
{
    _using_interrupt = true;
    _interrupt_num = interruptNum;
}

// Remove interrupt protection
void SPIClass::notUsingInterrupt(uint8_t interruptNum)
{
    if (_interrupt_num == interruptNum) {
        _using_interrupt = false;
        _interrupt_num = 0;
    }
}

// Select slave device
void SPIClass::selectSlave(uint8_t cs)
{
#if defined(CONFIG_SPI_SUPPORT_MASTER) && (CONFIG_SPI_SUPPORT_MASTER == 1)
    if (_initialized) {
        _selected_slave = (spi_slave_t)cs;
        uapi_spi_select_slave(SPI_BUS_0, _selected_slave);
    }
#else
    // Stub: No hardware slave selection without SDK support
    (void)cs; // Suppress unused parameter warning
#endif
}

// Global SPI instance
SPIClass SPI;
