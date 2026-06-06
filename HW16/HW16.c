#include <stdio.h>
#include "pico/stdlib.h"
#include "hx711.h"
#include "ina219.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "math.h"

#define IN1 13
#define IN2 12
#define ADC_PIN 26
#define BUF_SIZE 200

enum mode_t {IDLE, PWM, ITEST, HOLD};
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

volatile int state_read = 0;

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
    //20 khz pwm on In1, GP13
    gpio_init(IN1);
    gpio_set_dir(IN1, GPIO_OUT);
    gpio_set_function(IN1, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(IN1);
    float div = 1;
    pwm_set_clkdiv(slice_num, div);
    pwm_set_wrap(slice_num, 7500); // 20 kHz PWM frequency, must be less than 65536 for 16-bit counter
    pwm_set_enabled(slice_num, true);

    // 20 kHz pwm on In2, GP12
    gpio_init(IN2);
    gpio_set_dir(IN2, GPIO_OUT);
    gpio_set_function(IN2, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(IN2);
    pwm_set_clkdiv(slice_num, div);
    pwm_set_wrap(slice_num, 7500); // 20 kHz PWM frequency, must be less than 65536 for 16-bit counter
    pwm_set_enabled(slice_num, true);
}

void set_duty(float duty_percent){
    if (duty_percent >= 0) {
        pwm_set_gpio_level(IN1, 7500);
        pwm_set_gpio_level(IN2, (100-duty_percent)*7500/100);
    }
    else if (duty_percent < 0) {
        pwm_set_gpio_level(IN2, 7500);
        pwm_set_gpio_level(IN1, (100+duty_percent)*7500/100);
    }
}

// 1kH timer interrupt
bool current_control(struct repeating_timer *t) {
    uint16_t adc_value = adc_read();
    if (adc_value < 100 || adc_value > 1600) { // safety stop
            set_duty(0);
            return true;
        }


    // if (state_read == 1){
    //     // checking position and current
    //     float current = read_ina219();
    //     printf("Current: %.4f mA\n", current);

    //     // read ADC
    //     uint16_t adc_value = adc_read();
    //     printf("ADC Value: %d\n", adc_value);
    //     state_read = 0;
    // }


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
    }
    return true;
}

void init_ADC(){
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(0); // select ADC0, which is connected to GPIO26
}


int main()
{
    stdio_init_all();
    init_ina219();
    init_ADC();
    init_hbridge();
    init_desired_current();

    Kp_current = 0.007;  // tune these
    Ki_current = 0.025;

    sleep_ms(5000);

    add_repeating_timer_ms(-1, current_control, NULL, &timer);

    while (true) {

        char input;
        scanf(" %c", &input);
        if (input == 'a'){
            // state_read = 1;
            // while (state_read == 1) {
            //     tight_loop_contents();
            // }

            current_index = 0;
            error_integral = 0;
            mode = ITEST;

            while (mode == ITEST) { 
                tight_loop_contents();
            }

            printf("%d\n", 400);
            for (int i = 0; i < 400; i++) {
                printf("%.4f %.4f\n", desired_current[i], actual_current[i]);
            }

            sleep_ms(5000);
        }
    }

}