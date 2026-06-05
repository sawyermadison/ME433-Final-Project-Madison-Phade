#include <stdio.h>
#include "pico/stdlib.h"
#include "ina219.h"
#include "hardware/pwm.h"
#include "math.h"

#define INA_SDA_PIN 10
#define INA_SCL_PIN 11
#define DT_PIN 14
#define SCK_PIN 15
#define BUF_SIZE 200

enum mode_t {IDLE, PWM, ITEST, HOLD, TRACK};
volatile enum mode_t mode = IDLE;

volatile float duty = 0; // global variable for the duty cycle, between -100 and 100
volatile float desired_current[400];
volatile float actual_current[400];
volatile int current_index = 0;


volatile float Kp_current = 0;
volatile float Ki_current = 0;
volatile float error_integral = 0;

struct repeating_timer timer;
struct repeating_timer timer_position;

void init_desired_current(){
    for (int i = 0; i < 400; i++){
        if ((i/100) % 4 == 0 || (i/100) % 4 == 2){ // if we are in the first or third 100 iterations, we want -100mA
            desired_current[i] = -100;
        }
        else{ // if we are in the second or fourth 100 iterations, we want +100mA
            desired_current[i] = 100;
        }
    }
}

void init_hbridge(){
    //20 khz pwm on In1, GP17
    gpio_init(17);
    gpio_set_dir(17, GPIO_OUT);
    gpio_set_function(17, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(17);
    float div = 1;
    pwm_set_clkdiv(slice_num, div);
    pwm_set_wrap(slice_num, 7500); // 20 kHz PWM frequency, must be less than 65536 for 16-bit counter
    pwm_set_enabled(slice_num, true);

    // 20 kHz pwm on In2, GP16
    gpio_init(16);
    gpio_set_dir(16, GPIO_OUT);
    gpio_set_function(16, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(16);
    pwm_set_clkdiv(slice_num, div);
    pwm_set_wrap(slice_num, 7500); // 20 kHz PWM frequency, must be less than 65536 for 16-bit counter
    pwm_set_enabled(slice_num, true);
}

void set_duty(float duty_percent){
    if (duty_percent >= 0) {
        pwm_set_gpio_level(17, 7500);
        pwm_set_gpio_level(16, (100-duty_percent)*7500/100);
    }
    else if (duty_percent < 0) {
        pwm_set_gpio_level(16, 7500);
        pwm_set_gpio_level(17, (100+duty_percent)*7500/100);
    }
}

// 1kH timer interrupt
bool current_control(struct repeating_timer *t) {
    float error;
    switch(mode){
        case IDLE:
            set_duty(0);
            break;
        case PWM:
            set_duty(duty);
            break;
        case ITEST: //control the current to -100mA for 100 interations (0.1s at 1kHz), then to +100mA for 100 iterations, then back to -100mA, then back to +100mA, using the Kp and Ki current control gains
            
            if (current_index >= 400){
                current_index = 0;
                mode = IDLE;
                break;
            }
            
            actual_current[current_index] = read_ina219();
            error = desired_current[current_index] - actual_current[current_index];
            error_integral += error;
            
            duty = Kp_current*error + Ki_current*error_integral;
            set_duty(duty);

            current_index++;
            break;
        case HOLD:
            break;
        case TRACK:
            if (position_index >= trajectory_length){
                position_index = 0;
                error_integral = 0;
                mode = IDLE;
                break;
            }

            float measured = read_ina219();
            error = desired_current_for_position - measured;
            error_integral += error;
            
            duty = Kp_current*error + Ki_current*error_integral;
            
            if (duty > 100) duty = 100;
            if (duty < -100) duty = -100;
            set_duty(duty);

            current_index++;
            break;
    }
    return true;
}




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
        printf("%d\n", N_SAMPLES);
        for (int i = 0; i < N_SAMPLES; i++) {
            printf("%.4f %.4f\n", desired[i], actual[i]);
        }
        int num_samples;
        printf("Enter number of samples to collect: ");
        scanf("%d", &num_samples);
        printf("Collecting %d samples...\n", num_samples);
        hx711_collect_samples(num_samples);
    }
}

void interrupt_callback(void){
    read_ina219();
    // print out current along with position
    // read ADC
    // 
}