#include <stdio.h>
#include "hx711.h"
#include "pico/stdlib.h"

#define DT_PIN 14
#define SCK_PIN 15

void hx711_collect_samples(int num_samples) {

    int raw[num_samples];
    int32_t filtered[num_samples];
    uint32_t time[num_samples];

    int32_t avg = 0;
    for (int i = 0; i < 20; i++) {
        avg += read_bits();
    }
    avg /= 20;

    uint32_t t0 = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < num_samples; i++) {
        int32_t value = read_bits();
        printf("%d\n", value);

        raw[i] = value;

        avg = value * 0.1 + avg * 0.9;
        filtered[i] = avg;

        time[i] = to_ms_since_boot(get_absolute_time()) - t0;
    }

    for (int i = 0; i < num_samples; i++) {
        printf("%lu %ld %ld\n", time[i], raw[i], filtered[i]);
    }
}

void init_hx711(){
    gpio_init(DT_PIN);
    gpio_init(SCK_PIN);
    gpio_set_dir(SCK_PIN, GPIO_OUT);
    gpio_set_dir(DT_PIN, GPIO_IN);
}

int32_t read_bits(){
    uint32_t raw = 0;

    while (gpio_get(DT_PIN) != 0) { // wait until data pin is low
        tight_loop_contents();
    }
    for (int i=0; i<24; i++){
        gpio_put(SCK_PIN, 1);
        sleep_us(10);
        raw = gpio_get(DT_PIN) | (raw << 1); // shifting data left and appending new data
        gpio_put(SCK_PIN, 0);
        sleep_us(10);
    }
    gpio_put(SCK_PIN, 1); // flash SCK for 25th time
    sleep_us(10);
    gpio_put(SCK_PIN, 0);

    // sign-extend 24-bit two's complement to 32-bit signed int
    if (raw & 0x800000) {
        raw |= 0xFF000000;
    }
    return raw;
}
