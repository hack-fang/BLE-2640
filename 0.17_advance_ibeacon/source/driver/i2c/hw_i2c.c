#include "board.h"
#include <ti/drivers/I2C.h>

#include "hw_i2c.h"
#include "hw_uart.h"

/*********************************************************************
 * LOCAL PARAMETER
 */
I2C_Handle I2CHandle;
I2C_Params I2Cparams;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
//void I2C_transferCallback(I2C_Handle, I2C_Transaction *, bool);

/*********************************************************************
 * @fn      GY_I2cTask_Init
 *
 * @brief   I2C��ʼ��
 *
 * @param   .
 *
 * @return  None.
 */
void HwI2CInit(void)
{
  I2C_init();
  I2C_Params_init(&I2Cparams);
  I2Cparams.bitRate = I2C_100kHz;
  I2Cparams.custom = NULL;
  I2Cparams.transferCallbackFxn = NULL;
  I2Cparams.transferMode = I2C_MODE_BLOCKING;
  //I2Cparams.transferCallbackFxn = I2C_transferCallback;
  //I2Cparams.transferMode = I2C_MODE_CALLBACK;

  I2CHandle = I2C_open(CC2650_LAUNCHXL_I2C0,&I2Cparams);
}


/*********************************************************************
 * @fn      GY_I2C_SET
 *
 * @brief   ���üĴ��������Ĵ�����дֵ��
 *
 * @param   .
 *
 * @return  None.
 */
bool HwI2CSet(uint8_t SlaveAddr,uint8_t RegAddr, uint8_t WriteBuf)
{
  bool ret = false;
  uint8_t buf[2] = {0};
  buf[0] = RegAddr;
  buf[1] = WriteBuf;
  
  I2C_Transaction i2cTransaction;
  i2cTransaction.writeBuf = buf;
  i2cTransaction.writeCount = 2;
  i2cTransaction.readBuf = NULL;
  i2cTransaction.readCount = 0;
  i2cTransaction.slaveAddress = SlaveAddr;
  i2cTransaction.arg = NULL;
  ret = I2C_transfer(I2CHandle, &i2cTransaction);
  return ret;
}

bool HwI2CSet_LenByte(uint8_t SlaveAddr,uint8_t RegAddr, uint8_t *WriteBuf, uint8_t len)
{
  bool ret = false;
  uint8_t buf[2] = {0};
  buf[0] = RegAddr;
  for (uint8_t i = 1; i <= len; i++) {
    buf[i] = WriteBuf[i - 1];
  }

  I2C_Transaction i2cTransaction;
  i2cTransaction.writeBuf = buf;
  i2cTransaction.writeCount = len + 1;
  i2cTransaction.readBuf = NULL;
  i2cTransaction.readCount = 0;
  i2cTransaction.slaveAddress = SlaveAddr;
  i2cTransaction.arg = NULL;
  ret = I2C_transfer(I2CHandle, &i2cTransaction);
  return ret;
}

/*********************************************************************
 * @fn      GY_I2C_GET
 *
 * @brief   ��ȡ�Ĵ�������ȡ�Ĵ����е�ֵ��
 *
 * @param   .
 *
 * @return  None.
 */
bool HwI2CGet(uint8_t SlaveAddr, uint8_t RegAddr, uint8_t *ReadBuf, uint8_t ReadLen)
{
  bool ret = false;
  I2C_Transaction i2cTransaction;
  i2cTransaction.writeBuf = &RegAddr;
  i2cTransaction.writeCount = 1;
  i2cTransaction.readBuf = ReadBuf;
  i2cTransaction.readCount = ReadLen;
  i2cTransaction.slaveAddress = SlaveAddr;
  i2cTransaction.arg = NULL;
  ret = I2C_transfer(I2CHandle, &i2cTransaction);
  return ret;
}

bool HwI2CGetData(uint8_t SlaveAddr,uint8_t *ReadBuf, uint8_t ReadLen)
{
  bool ret = false;
  I2C_Transaction i2cTransaction;
  i2cTransaction.writeBuf = NULL;
  i2cTransaction.writeCount = 0;
  i2cTransaction.readBuf = ReadBuf;
  i2cTransaction.readCount = ReadLen;
  i2cTransaction.slaveAddress = SlaveAddr;
  i2cTransaction.arg = NULL;
  ret = I2C_transfer(I2CHandle, &i2cTransaction);
  return ret;
}
