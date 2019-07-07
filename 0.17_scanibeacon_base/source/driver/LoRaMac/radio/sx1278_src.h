/**
 * \file
 *         Head file for independent source codes to operate sx1278
 * \author
 *         Jiang Jun <jiangjunjie_2005@126.com>
 * \date
 *         2015-04-09 15:52
 * 
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SX1278_SRC_H__
#define __SX1278_SRC_H__

/* Includes ------------------------------------------------------------------*/
//#include "Util.h"
//#include "Dbg.h"
#include "../ports/radio.h"

/* Exported variables ------------------------------------------------------- */

/* Exported types ------------------------------------------------------------*/

/**
 * @brief  Spreading Factor of LoRa settings
 */
typedef enum
{
    RF_SF_6 = (uint8_t)6,
    RF_SF_7,
    RF_SF_8,
    RF_SF_9,
    RF_SF_10,
    RF_SF_11,
    RF_SF_12,
} RadioSF_t;

/**
 * @brief  Forward Error Correction of LoRa settings
 */
typedef enum
{
    RF_FEC_4_5 = (uint8_t)1,
    RF_FEC_4_6,
    RF_FEC_4_7,
    RF_FEC_4_8,
} RadioFEC_t;

/**
 * @brief  Bandwidth of LoRa settings
 */
typedef enum
{
    RF_BW_7800 = (uint8_t)0,
    RF_BW_10400,
    RF_BW_15600,    
    RF_BW_20800,    
    RF_BW_31250,    
    RF_BW_41700,    
    RF_BW_62500,    
    RF_BW_125000,    
    RF_BW_250000,    
    RF_BW_500000,    
} RadioBW_t;

/**
 * @brief  Selects PA output pin.
 *            RFO pin: Output power is limited to +14 dBm.
 *            PA_BOOST pin: Output power is limited to +20 dBm.
 */
typedef enum
{
    PA_OUTPUT_PIN_RFO = (uint8_t)0x00,
    PA_OUTPUT_PIN_BOOST = (uint8_t)0x80,
} PA_OUTPUT_PIN_TypeDef;

/* Exported constants --------------------------------------------------------*/
/**
* @brief  Size of radio FIFO.
*/
#define RF_FIFO_SIZE    255


/* Exported macros -----------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/

/* Exported functions ------------------------------------------------------- */
void SX1278Init(RadioEvents_t *p_stEvents);

RadioState_t SX1278GetStatus(void);
bool SX1278IsChannelFree(uint16_t wPeriodMs, int16_t nRssiThresh);
uint32_t SX1278Random(void);
bool SX1278IsReceiving(uint16_t wPeriodMs);
int16_t SX1278GetCurrentRssi(void);

void SX1278SetFreq(uint32_t freq);
void SX1278SetPAOutput(PA_OUTPUT_PIN_TypeDef ePAOutput);
void SX1278SetTxPower(int8_t chPower);

/**
  * @brief  Write the changed settings into registers of LoRa.
  * @note  MUST call this function after changed settings of LoRa.
  */
void SX1278SetLoRaSettings(void);
void SX1278SetPreambleLen(uint16_t wLen);
void SX1278SetBandwidth(RadioBW_t tBW);
void SX1278SetSpreadingFactor(RadioSF_t tSF);
void SX1278SetCodingRate(RadioFEC_t tFEC);
void SX1278SetLowDatarateOptimize(bool bOptimize);
void SX1278SetFixLen(bool bFixLen);
void SX1278SetCrcOn(bool bCrcOn);
void SX1278SetSymbTimeout(uint16_t wSymbTimeout);
void SX1278SetRxContinuous(bool bRxContinuous);
void SX1278SetTxTimeout(uint16_t wTxTimeout);
void SX1278SetIqInverted(bool bIqInverted);

uint16_t SX1278GetTimeOnAir(uint8_t byPayload);
uint16_t SX1278GetPreambleTime(void);
int16_t SX1278CalcPreambleNum(uint16_t wMs);
uint16_t SX1278CalcPreambleNumExt(uint16_t wMs, RadioBW_t tBW, RadioSF_t tSF);
uint16_t SX1278CalcCADPeriod(void);

void SX1278SetSleep(void);
void SX1278SetStandby(void);

int8_t SX1278PrepareTx(const uint8_t *p_byBuf, uint8_t bySize);
void SX1278Transmit(void);
int8_t SX1278Send(const void *p_vBuf, uint8_t bySize);
int8_t SX1278Receive(uint16_t wTimeout);
int8_t SX1278SetCAD(uint16_t wTimeout);

uint8_t SX1278Read(uint8_t addr);
void SX1278Write(uint8_t addr, uint8_t data);

void SX1278Dio0IrqHandler(void);
void SX1278Dio1IrqHandler(void);


#endif

/*--------------------------------------------------------------------------
                                                                       0ooo
                                                           ooo0     (   )
                                                            (   )      ) /
                                                             \ (      (_/
                                                              \_)
----------------------------------------------------------------------------*/

