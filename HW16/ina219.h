
// nick's code
void init_ina219(){
    // set the INA219 sensitivity - 10 bit, plus/minus160mV, 148us per sample
    unsigned short ina219_calValue = 1024;
    unsigned short ina219_config = 0b0011000010001111;
    writeINA219(INA219_REG_CALIBRATION, ina219_calValue);
    writeINA219(INA219_REG_CONFIG, ina219_config);
}

float read_ina219(){
    float ma = 0;
    signed short value = readINA219(INA219_REG_CURRENT);
    ma = value / 3.0;
    return ma;
}

// write 2 bytes
void writeINA219(int reg, int value){
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = value>>8;
    buf[2] = value&0xff;

    HAL_I2C_Master_Transmit(&hi2c2, INA219_ADDR<<1, buf, 3, 10);
}

// read 2 bytes
signed short readINA219(unsigned char reg){
    HAL_I2C_Master_Transmit(&hi2c2, INA219_ADDR<<1, &reg, 1, 10);
    uint8_t buffer[2];
    HAL_I2C_Master_Receive(&hi2c2, INA219_ADDR<<1, buffer, 2, 10);

    signed short value = (buffer[0]<<8)|buffer[1];
    return value;
}