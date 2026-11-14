#include "kbdef.h"
#include "user_sleep.h"
#include "user_init.h"
#include "settings.h"
#include "gpio.h"
#include "extint.h"

#ifdef SLEEP_ENABLE

// HARDWARE NOTE: the wake path is transcribed from the stock firmware (GM610 backup,
// power-down entry at 0x45DC-0x46CC) but not yet proven on this board. If a key
// does not wake it, unplug and replug the USB cable: that resets the MCU.

// Wired: sleep only when the PC suspends the bus (USB resume wakes it).
// Bluetooth: sleep after the editor's idle timer (user_settings.sleep_min), wake on a key.
user_sleep_mode_t user_sleep_supported(void)
{
    return (user_settings.conn_mode == 0) ? USER_SLEEP_RF : USER_SLEEP_USB;
}

// Nothing may source current while asleep: every LED sink and key column is driven low,
// the key rows stay inputs with pull-ups so a keypress pulls a row low and reaches the
// wake interrupt. Same order and masks as the stock firmware's teardown.
static void park_panel(void)
{
    P5PCR &= 0xF8; P5CR |= 0x07; P5 &= 0xF8; // columns 0-2 (P5.0-2)
    P3PCR &= 0xC0; P3CR |= 0x3F; P3 &= 0xC0; // columns 3-8 (P3.5-0)
    P2PCR &= 0xC0; P2CR |= 0x3F; P2 &= 0xC0; // columns 9-13 (+ P2.0, unused)
    P1PCR &= 0xC0; P1CR |= 0x3F; P1 &= 0xC0; // P1.0-5, unused on this board

    P0PCR &= 0xE3; P0CR |= 0x1C; P0 &= 0xE3; // RGB sinks P0.2-4
    P5PCR &= 0x7F; P5CR |= 0x80; P5 &= 0x7F; // RGB sink P5.7

    P6PCR = 0x00; P6CR = 0xFF; P6 = 0x00;    // RGB sinks P6.0-7

    P4PCR &= 0x84; P4CR |= 0x7B; P4 &= 0x84; // RGB sinks P4.0,1,3-6 (P4.2 ack, P4.7 sck keep pull-ups)

    P5PCR &= 0x9F; P5CR &= 0x9F; P5 &= 0x9F; // P5.5/5.6: floating inputs

    P7PCR |= 0xC0; P7CR |= 0xE0; P7 &= 0x1F; // P7.5-7: driven low
    P7_6 = 0;

    // Bluetooth chip lines back to inputs
    P0CR &= 0xBF; // P0.6 miso
    P4CR &= 0x7F; // P4.7 sck
    P0CR &= 0x7F; // P0.7 mosi
    P0CR &= 0xDF; // P0.5 mot
    P7CR &= 0xEF; // P7.4 cs
}

void user_sleep_prepare(void)
{
    park_panel();
    extint_wake_arm();
}

void user_sleep_wake(void)
{
    // The BK3632 ACK on P4.2 toggles constantly once RF is back up, so INT4 has
    // to go before normal operation resumes.
    extint_wake_disable();

    RF_BB_SPI_MOT = 0;
    GPIO_OUTPUT(0, RF_BB_SPI_MOT_P0_5);
    RF_BB_SPI_MOT = 0;

    user_gpio_init();
}

#endif // SLEEP_ENABLE
