#include "kbdef.h"
#include "user_init.h"
#include "pwm.h"
#include "gpio.h"

#define PWM_PERD 0x0100

#define PWM_PERDH_INIT ((uint8_t)(PWM_PERD >> 8))
#define PWM_PERDL_INIT ((uint8_t)(PWM_PERD))

void user_gpio_init();
void user_pwm_init();

void user_init()
{
    // USB interrupt to the highest priority (IPL1/IPH1 bit 0 mirror IEN1.EUSB), so it
    // pre-empts the tick ISR (matrix scan + LED subframes). With a fast LED refresh the
    // tick ISR is busy enough that, at equal priority, enumeration requests miss their
    // deadline and Windows reports "Device Descriptor Request Failed".
    IPL1 |= _EUSB;
    IPH1 |= _EUSB;

    user_gpio_init();
    user_pwm_init();
}

void user_gpio_init()
{
    DRVCON = DRVCON_UNLOCK_P2;
    P2DRV  = GPIO_DRIVE_25MA;

    DRVCON = DRVCON_UNLOCK_P3;
    P3DRV  = GPIO_DRIVE_25MA;

    DRVCON = DRVCON_UNLOCK_P5;
    P5DRV  = GPIO_DRIVE_25MA;

    DRVCON = DRVCON_LOCK;

    // key columns + RGB row sinks as outputs (sinks start low = LEDs off)
    GPIO_LOW(0, RGB_P0_MASK);
    GPIO_LOW(4, RGB_P4_MASK);
    GPIO_LOW(5, RGB_P5_MASK);
    GPIO_LOW(6, RGB_P6_MASK);
    GPIO_DIR_WRITE(0, RGB_P0_MASK);
    GPIO_DIR_WRITE(2, (uint8_t)(KB_C13_P2_1 | KB_C12_P2_2 | KB_C11_P2_3 | KB_C10_P2_4 | KB_C9_P2_5));
    GPIO_DIR_WRITE(3, (uint8_t)(KB_C8_P3_0 | KB_C7_P3_1 | KB_C6_P3_2 | KB_C5_P3_3 | KB_C4_P3_4 | KB_C3_P3_5));
    GPIO_DIR_WRITE(4, RGB_P4_MASK);
    GPIO_DIR_WRITE(5, (uint8_t)(KB_C0_P5_0 | KB_C1_P5_1 | KB_C2_P5_2 | RGB_P5_MASK));
    GPIO_DIR_WRITE(6, RGB_P6_MASK);

    GPIO_PULLUP_WRITE(5, (uint8_t)(KB_R3_P5_3 | KB_R4_P5_4));
    GPIO_PULLUP_WRITE(7, (uint8_t)(KB_R0_P7_1 | KB_R1_P7_2 | KB_R2_P7_3));

#ifdef RF_ENABLED
    // bb_spi drives these open-drain: latch high, and leave PxCR to its per-cycle toggling.
    GPIO_HIGH(7, RF_BB_SPI_CS_P7_4);
    GPIO_HIGH(4, RF_BB_SPI_SCK_P4_7);
    GPIO_HIGH(0, (RF_BB_SPI_MOSI_P0_7 | RF_BB_SPI_MOT_P0_5));
    GPIO_PULLUP_ON(0, (RF_BB_SPI_MISO_P0_6 | RF_BB_SPI_MOSI_P0_7 | RF_BB_SPI_MOT_P0_5));
    GPIO_PULLUP_ON(4, (RF_BB_SPI_ACK_P4_2 | RF_BB_SPI_SCK_P4_7));
    GPIO_PULLUP_ON(7, RF_BB_SPI_CS_P7_4);
#endif
}

void user_pwm_init()
{
    PWM0PERDH = PWM_PERDH_INIT;
    PWM0PERDL = PWM_PERDL_INIT;

    PWM1PERDH = PWM_PERDH_INIT;
    PWM1PERDL = PWM_PERDL_INIT;

    PWM4PERDH = PWM_PERDH_INIT;
    PWM4PERDL = PWM_PERDL_INIT;

    SET_PWM_DUTY(LED_PWM_C0, 0, 0);
    SET_PWM_DUTY(LED_PWM_C1, 0, 0);
    SET_PWM_DUTY(LED_PWM_C2, 0, 0);
    SET_PWM_DUTY(LED_PWM_C3, 0, 0);
    SET_PWM_DUTY(LED_PWM_C4, 0, 0);
    SET_PWM_DUTY(LED_PWM_C5, 0, 0);
    SET_PWM_DUTY(LED_PWM_C6, 0, 0);
    SET_PWM_DUTY(LED_PWM_C7, 0, 0);
    SET_PWM_DUTY(LED_PWM_C8, 0, 0);
    SET_PWM_DUTY(LED_PWM_C9, 0, 0);
    SET_PWM_DUTY(LED_PWM_C10, 0, 0);
    SET_PWM_DUTY(LED_PWM_C11, 0, 0);
    SET_PWM_DUTY(LED_PWM_C12, 0, 0);
    SET_PWM_DUTY(LED_PWM_C13, 0, 0);
}
