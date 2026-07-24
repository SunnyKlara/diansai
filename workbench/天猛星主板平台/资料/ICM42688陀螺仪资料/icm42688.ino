#include <Wire.h>

// ICM-42688 I²C 地址
#define ICM42688_ADDRESS 0x69  // 根据实际情况选择 0x68 或 0x69，ad0拉低0x68，拉高：0x69
#define WHO_AM_I_REG 0x75
#define EXPECTED_WHO_AM_I 0x47



// 初始化配置寄存器


// Bank 0
#define ICM42688_DEVICE_CONFIG 0x11
#define ICM42688_DRIVE_CONFIG 0x13
#define ICM42688_INT_CONFIG 0x14
#define ICM42688_FIFO_CONFIG 0x16
#define ICM42688_TEMP_DATA1 0x1D
#define ICM42688_TEMP_DATA0 0x1E
#define ICM42688_ACCEL_DATA_X1 0x1F
#define ICM42688_ACCEL_DATA_X0 0x20
#define ICM42688_ACCEL_DATA_Y1 0x21
#define ICM42688_ACCEL_DATA_Y0 0x22
#define ICM42688_ACCEL_DATA_Z1 0x23
#define ICM42688_ACCEL_DATA_Z0 0x24
#define ICM42688_GYRO_DATA_X1 0x25
#define ICM42688_GYRO_DATA_X0 0x26
#define ICM42688_GYRO_DATA_Y1 0x27
#define ICM42688_GYRO_DATA_Y0 0x28
#define ICM42688_GYRO_DATA_Z1 0x29
#define ICM42688_GYRO_DATA_Z0 0x2A
#define ICM42688_TMST_FSYNCH 0x2B
#define ICM42688_TMST_FSYNCL 0x2C
#define ICM42688_INT_STATUS 0x2D
#define ICM42688_FIFO_COUNTH 0x2E
#define ICM42688_FIFO_COUNTL 0x2F
#define ICM42688_FIFO_DATA 0x30
#define ICM42688_APEX_DATA0 0x31
#define ICM42688_APEX_DATA1 0x32
#define ICM42688_APEX_DATA2 0x33
#define ICM42688_APEX_DATA3 0x34
#define ICM42688_APEX_DATA4 0x35
#define ICM42688_APEX_DATA5 0x36
#define ICM42688_INT_STATUS2 0x37
#define ICM42688_INT_STATUS3 0x38
#define ICM42688_SIGNAL_PATH_RESET 0x4B
#define ICM42688_INTF_CONFIG0 0x4C
#define ICM42688_INTF_CONFIG1 0x4D
#define ICM42688_PWR_MGMT0 0x4E
#define ICM42688_GYRO_CONFIG0 0x4F
#define ICM42688_ACCEL_CONFIG0 0x50
#define ICM42688_GYRO_CONFIG1 0x51
#define ICM42688_GYRO_ACCEL_CONFIG0 0x52
#define ICM42688_ACCEL_CONFIG1 0x53
#define ICM42688_TMST_CONFIG 0x54
#define ICM42688_APEX_CONFIG0 0x56
#define ICM42688_SMD_CONFIG 0x57
#define ICM42688_FIFO_CONFIG1 0x5F
#define ICM42688_FIFO_CONFIG2 0x60
#define ICM42688_FIFO_CONFIG3 0x61
#define ICM42688_FSYNC_CONFIG 0x62
#define ICM42688_INT_CONFIG0 0x63
#define ICM42688_INT_CONFIG1 0x64
#define ICM42688_INT_SOURCE0 0x65
#define ICM42688_INT_SOURCE1 0x66
#define ICM42688_INT_SOURCE3 0x68
#define ICM42688_INT_SOURCE4 0x69
#define ICM42688_FIFO_LOST_PKT0 0x6C
#define ICM42688_FIFO_LOST_PKT1 0x6D
#define ICM42688_SELF_TEST_CONFIG 0x70
#define ICM42688_WHO_AM_I 0x75
#define ICM42688_REG_BANK_SEL 0x76

#define GYRO_SENSITIVITY 1000.0 / 32768.0  //1000°/s，
#define ACC_SENSITIVITY 4 / 32768.0      //4g，
#define DEG_TO_RAD 0.017453292519943
#define RAD_TO_DEG 57.29577951308232


int16_t ax, ay, az, gx, gy, gz;
void setup() {
  Serial.begin(115200);   // 初始化串口
  Wire.begin();           // 初始化 I²C 总线
  Wire.setClock(400000);  // 400kbit/s I2C clock sda21 scl22

  Serial.println("ICM-42688 初始化...");
  uint8_t whoAmI = readReg(ICM42688_ADDRESS, WHO_AM_I_REG);
  if (whoAmI == EXPECTED_WHO_AM_I) {
    Serial.println("ICM-42688 检测成功!");
    //initializeICM42688();
  } else {
    Serial.print("检测失败! WHO_AM_I 返回: 0x");
    Serial.println(whoAmI, HEX);
    while (1)
      ;  // 停止程序，直到错误解决
  }
  writeReg(ICM42688_ADDRESS, ICM42688_REG_BANK_SEL, 0);      //设置bank 0区域寄存器
  writeReg(ICM42688_ADDRESS, ICM42688_DEVICE_CONFIG, 0x01);  //软复位传感器
  delay(100);
  writeReg(ICM42688_ADDRESS, ICM42688_ACCEL_CONFIG0, 0x48);  //量程 ±4g 输出速率 100HZ
  writeReg(ICM42688_ADDRESS, ICM42688_GYRO_CONFIG0, 0x28);   //量程 ±1000dps 输出速率 100HZ
  writeReg(ICM42688_ADDRESS, ICM42688_PWR_MGMT0, 0x0f);      //陀螺和加速度计LN mode
  delay(100);
}

void loop() {

  ICM42688_getacc(&ax, &ay, &az);

  ICM42688_getgyro(&gx, &gy, &gz);



  Serial.print(ax * ACC_SENSITIVITY);
  Serial.print("\t");
  Serial.print(ay * ACC_SENSITIVITY);
  Serial.print("\t");
  Serial.print(az * ACC_SENSITIVITY);
  Serial.print("\t");
  Serial.print(gx*GYRO_SENSITIVITY );
  Serial.print("\t");
  Serial.print(gy*GYRO_SENSITIVITY );
  Serial.print("\t");
  Serial.print(gz*GYRO_SENSITIVITY );
  Serial.println("\t");
  delay(20);  // 每隔500ms读取一次
}


void ICM42688_getacc(int16_t* ax, int16_t* ay, int16_t* az) {

  uint8_t buffer[6];                                                  // x/y/z accel register data stored here
  readRegs(ICM42688_ADDRESS, ICM42688_ACCEL_DATA_X1, 6, &buffer[0]);  // Read the 12 raw data registers into data array
  *ax = buffer[0] << 8 | buffer[1];                                   // Turn the MSB and LSB into a signed 16-bit value
  *ay = buffer[2] << 8 | buffer[3];
  *az = buffer[4] << 8 | buffer[5];
}

void ICM42688_getgyro(int16_t* gx, int16_t* gy, int16_t* gz) {

  uint8_t buffer[6];                                                 // x/y/z accel register data stored here
  readRegs(ICM42688_ADDRESS, ICM42688_GYRO_DATA_X1, 6, &buffer[0]);  // Read the 12 raw data registers into data array
  *gx = buffer[0] << 8 | buffer[1];                                  // Turn the MSB and LSB into a signed 16-bit value
  *gy = buffer[2] << 8 | buffer[3];
  *gz = buffer[4] << 8 | buffer[5];
}



void writeReg(uint8_t address, uint8_t subAddress, uint8_t data)  //写入寄存器一个字节
{
  Wire.beginTransmission(address);  // 从器件IIC地址
  Wire.write(subAddress);           //  访问寄存器地址
  Wire.write(data);                 // 写入的数据
  Wire.endTransmission();           //开始IIC通信
}


uint8_t readReg(uint8_t address, uint8_t subAddress)  //从寄存器读取一个字节
{
  uint8_t data = 0;
  Wire.beginTransmission(address);  // 从设备地址
  Wire.write(subAddress);           // 访问的寄存器地址
  Wire.endTransmission(false);      // false：不释放总线 继续通信
  Wire.requestFrom(address, 1);     // Read bytes from slave register address
  data = Wire.read();               // Put read results in the Rx buffer
  return data;
}


void readRegs(uint8_t address, uint8_t subAddress, uint8_t count, uint8_t data[])  //从寄存器读取多个字节
{
  uint8_t i = 0;
  Wire.beginTransmission(address);   // 从设备地址
  Wire.write(subAddress);            // 访问的寄存器地址
  Wire.endTransmission(false);       // false：不释放总线 继续通信
  Wire.requestFrom(address, count);  // Read bytes from slave register address
  while (Wire.available()) {
    data[i++] = Wire.read();  // Put read results in the Rx buffer
  }
}
