#include <stdio.h>
#include "ina219.h"
#include "hx711.h"
#include "pico/stdlib.h"

#define INA_SDA_PIN 10
#define INA_SCL_PIN 11
#define DT_PIN 14
#define SCK_PIN 15

int main()
{
    stdio_init_all();
    init_hx711();
    
    sleep_ms(5000);
    printf("Starting data collection...\n");

    //  while (gpio_get(DT_PIN) != 0) { // wait until data pin is low
    //     tight_loop_contents();
    //  }

    //  init_ina219(INA_SDA_PIN, INA_SCL_PIN);
    //  sleep_ms(1000);

    //  printf("INA219 and HX711 initialized\n");

    //  while (true) {
    //     float voltage = read_ina219_voltage();
    //     float current = read_ina219_current();

    //     printf("Voltage: %.3f V, Current: %.3f mA\n", voltage, current);
    //     sleep_ms(1000);
    //  }

    while (true) {
        int num_samples;
        printf("Enter number of samples to collect: ");
        scanf("%d", &num_samples);
        printf("Collecting %d samples...\n", num_samples);
        hx711_collect_samples(num_samples);
    }
}
