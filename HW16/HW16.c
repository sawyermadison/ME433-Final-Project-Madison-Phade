#include <stdio.h>
#include "ina219.h"
#include "pico/stdlib.h"

#define INA_SDA_PIN 10
#define INA_SCL_PIN 11
#define DT_PIN 14
#define SCK_PIN 15

int main()
{
    stdio_init_all();

    while (true) {
        printf("Hello, Anisha!\n");
        sleep_ms(1000);
    }
}
