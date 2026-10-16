/*
 * @Author: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @Date: 2024-07-22 09:19:25
 * @LastEditors: error: error: git config user.name & please set dead value or install git && error: git config user.email & please set dead value or install git & please set dead value or install git
 * @LastEditTime: 2024-07-22 10:30:45
 * @FilePath: \PlatformIO\src\f6336u_device.cpp
 * @Description: 
 * 
 * Copyright (c) 2024 by ${git_name_email}, All Rights Reserved. 
 */
#include "cst816d.h"
#include <Wire.h>
TwoWire touch_i2c = TwoWire(1);         


static bool isCst816d_present(void)
{
    return false;
}

static bool touch_i2c_write(uint8_t driver_addr, uint8_t reg_addr, const uint8_t *data, uint32_t length)
{
    touch_i2c.beginTransmission(driver_addr);
    touch_i2c.write(reg_addr);
    touch_i2c.write(data, length);


    if (touch_i2c.endTransmission() != 0) {
        return false;
    }
    return true;
}

static bool touch_i2c_read(uint8_t driver_addr, uint8_t reg_addr, uint8_t *data, uint32_t length)
{
    touch_i2c.beginTransmission(driver_addr);
    touch_i2c.write(reg_addr);
    if (touch_i2c.endTransmission() != 0) {
        return false;
    }

    touch_i2c.requestFrom(driver_addr, length);
    if (touch_i2c.available() != length) {
        return false;
    }
    touch_i2c.readBytes(data, length);
    return true; // 读取成功
}

bool touch_init(int tp_sda, int tp_scl, int tp_rst, int tp_int)
{
    // 初始化i2C
    touch_i2c.begin(tp_sda, tp_scl, TP_I2C_FREQ);
    pinMode(tp_rst, OUTPUT);
    // 复位
    digitalWrite(tp_rst, LOW);
    delay(200);
    digitalWrite(tp_rst, HIGH);
    delay(300);

    uint8_t data[5] = {0};
    touch_i2c_read(CST816D_ADDR, CST816D_ID_REG, data, 1);
    if (data[0] == 0xB6){
        Serial.printf("Find CST816T!\n");
    }
    return true;
}


bool get_touch_coords(toucht_coords_t *toucht_coords)
{
    uint8_t x_arr[2] = {0};
    uint8_t y_arr[2] = {0};
    uint8_t touch_num = 0;


    if (!touch_i2c_read(CST816D_ADDR, CST816D_TOUCH_NUM_REG, &touch_num, 1))
        return false;
    touch_num &= 0x0f;
    if (touch_num == 0)
        return false;
    

    if (!touch_i2c_read(CST816D_ADDR, CST816D_TOUCH_XH_REG, x_arr, 1)) return false;
    if (!touch_i2c_read(CST816D_ADDR, CST816D_TOUCH_XL_REG, x_arr + 1, 1)) return false;
    if (!touch_i2c_read(CST816D_ADDR, CST816D_TOUCH_YH_REG, y_arr, 1)) return false;
    if (!touch_i2c_read(CST816D_ADDR, CST816D_TOUCH_YL_REG, y_arr + 1, 1)) return false;

    toucht_coords->x = (uint16_t)(( x_arr[0] & 0x0f) <<  8);
    toucht_coords->x |= x_arr[1];
    toucht_coords->y = (uint16_t)((y_arr[0] & 0x0f) <<  8);
    toucht_coords->y |= y_arr[1];

    return true;
}

