// function prototypes for the HX711 load cell

#ifndef HX711_H
#define HX711_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

void init_hx711();
int32_t read_bits();;
void hx711_collect_samples(int num_samples);

#endif