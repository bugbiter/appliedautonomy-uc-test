#include <stdio.h>
#include "bcm2835.h"

#define PIN RPI_BPLUS_GPIO_J8_21

int main()
{
    if (!bcm2835_init())
        return 1;

    printf("Let's try to read an input with the bcm2835 library!");
    // set RPI pin to be an input
    bcm2835_gpio_fsel(PIN, BCM2835_GPIO_FSEL_INPT);
    // with a pullup
    bcm2835_gpio_set_pud(PIN, BCM2835_GPIO_PUD_UP);
    while (1)
    {
        // Read some data
        uint8_t value = bcm2835_gpio_lev(PIN);
        printf("GPIO21: %d\n", value);
        // wait a bit
        delay(1000);
    }
    bcm2835_close();
    return 0;
}