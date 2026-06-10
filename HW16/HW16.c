#include <stdio.h>
#include "pico/stdlib.h"
#include "hx711.h"
#include "ina219.h"
#include "as5600.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "math.h"
#include "hardware/i2c.h"
#include "as5600.h"

#define IN1 13
#define IN2 12
#define ADC_PIN 26
#define BUF_SIZE 200

#define I2C_PORT    i2c0
#define PIN_SDA     4
#define PIN_SCL     5
#define I2C_FREQ    400000   /* 400 kHz fast mode */

// Wall parameters (in degrees)
#define WALL_LEFT   330.0f
#define WALL_RIGHT  30.0f
#define WALL_STIFFNESS  30.0f   // mA per degree of penetration, tune this

// Transparency compensation
#define FRICTION_COMP  15.0f   // mA, tune to taste — constant torque to overcome motor friction



static const char *magnet_status_str(as5600_magnet_status_t s)
{
    switch (s) {
        case AS5600_MAGNET_NOT_DETECTED: return "NOT DETECTED";
        case AS5600_MAGNET_OK:           return "OK";
        case AS5600_MAGNET_TOO_WEAK:     return "TOO WEAK";
        case AS5600_MAGNET_TOO_STRONG:   return "TOO STRONG";
        default:                         return "UNKNOWN";
    }
}

enum mode_t {IDLE, PWM, ITEST, HOLD, HAPTIC};
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

as5600_t sensor;



float get_desired_current(float angle_deg) {
    // Right wall: small positive angles (e.g. > 45 degrees rightward)
    if (angle_deg > 0.5f && angle_deg < 180.0f) {
        float penetration = angle_deg - WALL_RIGHT;  // how far past 45
        if (penetration > 0){
            printf("Right detected: %.2f deg\n", penetration);
            return -(WALL_STIFFNESS * penetration);  // push back left toward 0
        }
    }
    // Left wall: large angles close to 360 (e.g. < 315 degrees leftward)
    else if (angle_deg >= 180.0f) {
        float penetration = WALL_LEFT - angle_deg;  // positive when past the wall
        if (penetration > 0){
            printf("Left detected: %.2f deg\n", penetration);
            return (WALL_STIFFNESS * penetration);    // push back rightward (positive current)
        }
    }
    // Free zone: near 0, between the walls
    return 0;
}


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
    //printf("ADC Value: %d\n", adc_value);
    if (adc_value < 50 || adc_value > 2000) { // safety stop
            printf("out of range\n");
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
        case HAPTIC:
            float angle;
            as5600_read_angle_degrees(&sensor, &angle);

            printf("Angle: %.2f deg\n", angle);

            float desired = get_desired_current(angle);

            float actual = read_ina219();
            float error = desired - actual;
            error_integral += error;

            // Clamp integral to prevent windup
            if (error_integral >  5000.0f) error_integral =  5000.0f;
            if (error_integral < -5000.0f) error_integral = -5000.0f;

            duty = Kp_current * error + Ki_current * error_integral;

            // Clamp duty
            if (duty >  100.0f) duty =  100.0f;
            if (duty < -100.0f) duty = -100.0f;

            set_duty(duty);
            break;
    }
    return true;
}

void init_ADC(){
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(0); // select ADC0, which is connected to GPIO26
}


// int main()
// {
//     stdio_init_all();
//     init_ina219();
//     init_ADC();
//     init_hbridge();
//     init_desired_current();

//     Kp_current = 0.007;  // tune these
//     Ki_current = 0.025;

//     sleep_ms(5000);

//     add_repeating_timer_ms(-1, current_control, NULL, &timer);

//     while (true) {

//         char input;
//         scanf(" %c", &input);
//         if (input == 'a'){
//             // state_read = 1;
//             // while (state_read == 1) {
//             //     tight_loop_contents();
//             // }

//             current_index = 0;
//             error_integral = 0;
//             mode = ITEST;

//             while (mode == ITEST) { 
//                 tight_loop_contents();
//             }

//             printf("%d\n", 400);
//             for (int i = 0; i < 400; i++) {
//                 printf("%.4f %.4f\n", desired_current[i], actual_current[i]);
//             }

//             sleep_ms(5000);
//         }
//     }

// }

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);   /* Wait for USB serial to enumerate */

    init_ina219();
    init_ADC();
    init_hbridge();
    init_desired_current();

    Kp_current = 0.007;  // tune these
    Ki_current = 0.025;

    add_repeating_timer_ms(-1, current_control, NULL, &timer);
 
    /* ── I2C initialisation ── */
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);
 
    /* ── AS5600 initialisation ── */
    //as5600_t sensor;
    if (!as5600_init(&sensor, I2C_PORT, AS5600_I2C_ADDR)) {
        printf("AS5600 not found! Check wiring.\n");
        while (1) tight_loop_contents();
    }
    printf("AS5600 initialised.\n");
 
    /* Optional: set 2 LSB hysteresis to reduce jitter at standstill */
    as5600_set_hysteresis(&sensor, AS5600_HYST_2LSB);
 
    /* Optional: zero the sensor at the current position */
    as5600_zero_here(&sensor);
 
    while (1) {

        // float    degrees;
        // uint16_t raw;
        // uint8_t  agc;
        // as5600_magnet_status_t mag = as5600_magnet_status(&sensor);
 
        // bool ok_deg = as5600_read_angle_degrees(&sensor, &degrees);
        // bool ok_raw = as5600_read_raw_angle(&sensor, &raw);
        // bool ok_agc = as5600_read_agc(&sensor, &agc);
 
        // if (ok_deg && ok_raw) {
        //     printf("Angle: %7.2f deg  Raw: %4u  AGC: %3u  Magnet: %s\n",
        //           degrees, raw, ok_agc ? agc : 0,
        //           magnet_status_str(mag));
        // } else {
        //     printf("Read error!\n");
        // }
 
        //sleep_ms(100);

        mode = HAPTIC;

    }
 
    return 0;
}