/******************************************************************************

 @file  simple_peripheral.c

 @brief This file contains the Simple BLE Peripheral sample application for use
        with the CC2650 Bluetooth Low Energy Protocol Stack.

 Group: WCS, BTS
 Target Device: CC2650, CC2640, CC1350

 ******************************************************************************
 
 Copyright (c) 2013-2016, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 Release Name: ble_sdk_2_02_01_18
 Release Date: 2016-10-26 15:20:04
 *****************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include <string.h>

#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Queue.h>

#include "hci_tl.h"
#include "gatt.h"
#include "linkdb.h"
#include "gapgattserver.h"
#include "gattservapp.h"
#include "devinfoservice.h"

#if defined(FEATURE_OAD) || defined(IMAGE_INVALIDATE)
#include "oad_target.h"
#include "oad.h"
#endif //FEATURE_OAD || IMAGE_INVALIDATE

#include "peripheral.h"
#include "gapbondmgr.h"

#include "osal_snv.h"
#include "icall_apimsg.h"

#include "util.h"

#ifdef USE_RCOSC
#include "rcosc_calibration.h"
#endif //USE_RCOSC

#include <ti/mw/display/Display.h>
#include "iotboard_key.h"

#include "board.h"

#include "simple_peripheral.h"

#if defined( USE_FPGA ) || defined( DEBUG_SW_TRACE )
#include <driverlib/ioc.h>
#endif // USE_FPGA | DEBUG_SW_TRACE

#include "task_uart.h" 
#include "hw_gpio.h"
#include "iBeaconProfile.h"
/*********************************************************************
 * CONSTANTS
 */

// Advertising interval when device is discoverable (units of 625us, 160=100ms)
#define DEFAULT_ADVERTISING_INTERVAL          160

// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely
#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_GENERAL

#ifndef FEATURE_OAD
// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic
// parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     10

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic
// parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     10
#else //!FEATURE_OAD
// Minimum connection interval (units of 1.25ms, 8=10ms) if automatic
// parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     8

// Maximum connection interval (units of 1.25ms, 8=10ms) if automatic
// parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     8
#endif // FEATURE_OAD

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         0

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter
// update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          100

// Whether to enable automatic parameter update request when a connection is
// formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         GAPROLE_LINK_PARAM_UPDATE_INITIATE_BOTH_PARAMS

// Connection Pause Peripheral time value (in seconds)
#define DEFAULT_CONN_PAUSE_PERIPHERAL         6

// How often to perform periodic event (in msec)
#define SBP_PERIODIC_EVT_PERIOD               60000
#define SBP_VBAT_EVT_PERIOD                   10000

#ifdef FEATURE_OAD
// The size of an OAD packet.
#define OAD_PACKET_SIZE                       ((OAD_BLOCK_SIZE) + 2)
#endif // FEATURE_OAD

// Task configuration
#define SBP_TASK_PRIORITY                     1


#ifndef SBP_TASK_STACK_SIZE
#define SBP_TASK_STACK_SIZE                   644
#endif

// Internal Events for RTOS application
#define SBP_STATE_CHANGE_EVT                  0x0001
#define SBP_CHAR_CHANGE_EVT                   0x0002
#define SBP_PERIODIC_EVT                      0x0004
#define SBP_CONN_EVT_END_EVT                  0x0008
#define SBP_KEY_CHANGE_EVT                    0x0010

#define SBP_VBAT_CHANGE_EVT                   0x0020    //User define
/*********************************************************************
 * TYPEDEFS
 */

// App event passed from profiles.
typedef struct
{
  appEvtHdr_t hdr;  // event header.
} sbpEvt_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */

// Display Interface
Display_Handle dispHandle = NULL;

/*********************************************************************
 * LOCAL VARIABLES
 */

// Entity ID globally used to check for source and/or destination of messages
static ICall_EntityID selfEntity;

// Semaphore globally used to post events to the application thread
static ICall_Semaphore sem;

// Clock instances for internal periodic events.
static Clock_Struct periodicClock;
static Clock_Struct batteryClock;       //User define

// Queue object used for app messages
static Queue_Struct appMsg;
static Queue_Handle appMsgQueue;

static Queue_Struct uartRxMsg;
static Queue_Handle uartRxQueue;

#if defined(FEATURE_OAD)
// Event data from OAD profile.
static Queue_Struct oadQ;
static Queue_Handle hOadQ;
#endif //FEATURE_OAD

// events flag for internal application events.
static uint16_t events;

// Task configuration
Task_Struct sbpTask;
Char sbpTaskStack[SBP_TASK_STACK_SIZE];

// Profile state and parameters
//static gaprole_States_t gapProfileState = GAPROLE_INIT;

#define IB_SCAN_INDEX_NAME  2 
#define IB_SCAN_INDEX_MAC   17
#define IB_SCAN_INDEX_MAJOR 23
#define IB_SCAN_INDEX_MINOR 25
#define IB_SCAN_INDEX_BATT 27
// GAP - SCAN RSP data (max size = 31 bytes)
static uint8_t scanRspData[] =
{
  /*DeviceName
  这里调试遇到一个问题，如果localname不在前面则无法被ios识别
  */
  0x0A,//index 0 固定10，如果name不足9，自动补\0
  GAP_ADTYPE_LOCAL_NAME_COMPLETE,//index 21
  'i',//index 2
  'B',
  'e',
  'a',
  'c',
  'o',
  'n',
  ' ',
  ' ',//index 30
  
  /*PreFix*/
  0x10,//len of service index 11
  GAP_ADTYPE_SERVICE_DATA,//index 12
  0x0A,//Device Information    //index 13
  0x18,//Device Information    //index 14

  /*GhostyuIdentify*/
  0x47,//'G'  index 15 ollow 4bytes,is ghostyu identify
  0x59,//'Y'  iBeacon Version

  /*MacAddress*/
  0x41,//index 17follow 6bytes is mac address,because ios can't get peripheral mac address
  0x41,
  0x41,
  0x41,
  0x41,
  0x41,

  /*Major Value (2 Bytes)*/
  0x27,0x12,/*index:25 26*/
  
  /*Minor Value (2 Bytes)*/
  0x0B,0x86,/*index:27 28*/

  /*batt service*/
  0x64,//init with 100%(3.0v),0%(2.0v),index 27
};

#define IB_ADV_INDEX_UUID			9
#define IB_ADV_INDEX_MAJOR			21
#define IB_ADV_INDEX_MINOR			23
#define IB_ADV_INDEX_MEASUREDPOWER  	29
// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
static uint8_t advertData[] =
{
  0x02,   // length of this data
  GAP_ADTYPE_FLAGS,
  DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

  // in this peripheral
  0x1A,   // length of this data 26byte
  GAP_ADTYPE_MANUFACTURER_SPECIFIC,  //0xFF
  /*Apple Pre-Amble*/
  0x4C,//company id from SIG
  0x00,//company id from SIG
  0x02,//ibeacon request
  0x15,//length of below
  /*Device UUID (12 Bytes)*/
  0x57,0x69,0x73,0x64, 0x6f,0x6d,0x53,0x61, 0x66,0x65,0x74,0x79,
  /*Major Value (2 Bytes)*/
  0x27,0x12,/*index:21 22*/
  
  /*Minor Value (2 Bytes)*/
  0x0B,0x86,/*index:23 24*/
  
  /* User-defined Data (4 Bytes)*/
  0x55,0x00,0x00,0x00,
  /*Measured Power*/
  0xBE /*index:29*/ //the value is tuning,do not modify
};

// GAP GATT Attributes
static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "iBeacon";

// Globals used for ATT Response retransmission
static gattMsgEvent_t *pAttRsp = NULL;
static uint8_t rspTxRetry = 0;


/*********************************************************************
 * LOCAL VARIABLES
 */
/*测量功率*/
static int8_t MeasuredPower = 0xBE;
static int8_t MeasuredPower_Factory = 0xBE;
/*Major*/                                                    
static uint8_t MajorValue[2] = {0x27,0x12};
static uint8_t MajorValue_Factory[2] = {0x27,0x12};
/*Minor*/
static uint8_t MinorValue[2] = {0x0B,0x86};
static uint8_t MinorValue_Factory[2] = {0x0B,0x86};
/*ProximityUUID*/
static uint8_t iBeaconUUID[12]	= {0x57,0x69,0x73,0x64,0x6f,0x6d,0x53,0x61,0x66,0x65,0x74,0x79};
static uint8_t iBeaconUUID_Factory[12]	= {0xFD,0xA5,0x06,0x93,  0xA4,0xE2,0x4F,0xB1,  0xAF,0xCF,0xC6,0xEB};
/*发射功率*/
static uint8_t TxPower = LL_EXT_TX_POWER_MINUS_15_DBM;
//static uint8_t TxPower_Factory = LL_EXT_TX_POWER_MINUS_15_DBM;
/*广播间隔*/
static uint16_t AdvInterval = 20;
static uint16_t AdvInterval_Factory = 20;
/*密码,20s,可以手动输入，是否需要手动输入*/
static uint8_t Password[6] = {'1','2','3','4','5','6'};

/*mac地址*/
static uint8_t macAddr[6];

/*SNV地址*/
#define IB_SNV_MEASUREDPOWER    0x80
#define IB_SNV_MAJORVALUE       0x81
#define IB_SNV_MINORVALUE       0x82
#define IB_SNV_IBEACONUUID      0x83
#define IB_SNV_ADVINTERVAL      0x84
/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void SimpleBLEPeripheral_init( void );
static void SimpleBLEPeripheral_taskFxn(UArg a0, UArg a1);

static uint8_t SimpleBLEPeripheral_processStackMsg(ICall_Hdr *pMsg);
static uint8_t SimpleBLEPeripheral_processGATTMsg(gattMsgEvent_t *pMsg);
static void SimpleBLEPeripheral_processAppMsg(sbpEvt_t *pMsg);
static void SimpleBLEPeripheral_processStateChangeEvt(gaprole_States_t newState);
static void SimpleBLEPeripheral_processCharValueChangeEvt(uint8_t paramID);
static void SimpleBLEPeripheral_clockHandler(UArg arg);
static void SimpleBLEPeripheral_batteryclockHandler(UArg a0);   //User define-10.12

static void SimpleBLEPeripheral_sendAttRsp(void);
static void SimpleBLEPeripheral_freeAttRsp(uint8_t status);

static void SimpleBLEPeripheral_stateChangeCB(gaprole_States_t newState);
#ifndef FEATURE_OAD_ONCHIP
static void SimpleBLEPeripheral_charValueChangeCB(uint8_t paramID);
#endif //!FEATURE_OAD_ONCHIP
static void SimpleBLEPeripheral_enqueueMsg(uint8_t event, uint8_t state);

#ifdef FEATURE_OAD
void SimpleBLEPeripheral_processOadWriteCB(uint8_t event, uint16_t connHandle,
                                           uint8_t *pData);
#endif //FEATURE_OAD

void TransUartReceiveDataCallback(uint8_t *buf, uint16_t len);
void Simple_Peripheral_NotiData(uint8_t *buf, uint16_t len);
void SimpleBLEPeripheral_keyChangeHandler(uint8_t keys);
static void SimpleBLEPeripheral_handleKeys(uint8_t shift, uint8_t keys);
void RestoreFactorySettings(void);
void VBatCapacity(void);        //User define
void DefaultAdvertData(void);   //User define
/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t SimpleBLEPeripheral_gapRoleCBs =
{
  SimpleBLEPeripheral_stateChangeCB     // Profile State Change Callbacks
};

// GAP Bond Manager Callbacks
static gapBondCBs_t simpleBLEPeripheral_BondMgrCBs =
{
  NULL, // Passcode callback (not used by application)
  NULL  // Pairing / Bonding state Callback (not used by application)
};

// Simple GATT Profile Callbacks
#ifndef FEATURE_OAD_ONCHIP
static iBeaconProfileCBs_t SimpleBLEPeripheral_simpleProfileCBs =
{
  SimpleBLEPeripheral_charValueChangeCB // Characteristic value change callback
};
#endif //!FEATURE_OAD_ONCHIP

#ifdef FEATURE_OAD
static oadTargetCBs_t simpleBLEPeripheral_oadCBs =
{
  SimpleBLEPeripheral_processOadWriteCB // Write Callback.
};
#endif //FEATURE_OAD

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      SimpleBLEPeripheral_createTask
 *
 * @brief   Task creation function for the Simple BLE Peripheral.
 *
 * @param   None.
 *
 * @return  None.
 */
void SimpleBLEPeripheral_createTask(void)
{
  Task_Params taskParams;

  // Configure task
  Task_Params_init(&taskParams);
  taskParams.stack = sbpTaskStack;
  taskParams.stackSize = SBP_TASK_STACK_SIZE;
  taskParams.priority = SBP_TASK_PRIORITY;

  Task_construct(&sbpTask, SimpleBLEPeripheral_taskFxn, &taskParams, NULL);
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_init
 *
 * @brief   Called during initialization and contains application
 *          specific initialization (ie. hardware initialization/setup,
 *          table initialization, power up notification, etc), and
 *          profile initialization/setup.
 *
 * @param   None.
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_init(void)
{
  // ******************************************************************
  // N0 STACK API CALLS CAN OCCUR BEFORE THIS CALL TO ICall_registerApp
  // ******************************************************************
  // Register the current thread as an ICall dispatcher application
  // so that the application can send and receive messages.
  ICall_registerApp(&selfEntity, &sem);

#ifdef USE_RCOSC
  RCOSC_enableCalibration();
#endif // USE_RCOSC

#if defined( USE_FPGA )
  // configure RF Core SMI Data Link
  IOCPortConfigureSet(IOID_12, IOC_PORT_RFC_GPO0, IOC_STD_OUTPUT);
  IOCPortConfigureSet(IOID_11, IOC_PORT_RFC_GPI0, IOC_STD_INPUT);

  // configure RF Core SMI Command Link
  IOCPortConfigureSet(IOID_10, IOC_IOCFG0_PORT_ID_RFC_SMI_CL_OUT, IOC_STD_OUTPUT);
  IOCPortConfigureSet(IOID_9, IOC_IOCFG0_PORT_ID_RFC_SMI_CL_IN, IOC_STD_INPUT);

  // configure RF Core tracer IO
  IOCPortConfigureSet(IOID_8, IOC_PORT_RFC_TRC, IOC_STD_OUTPUT);
#else // !USE_FPGA
  #if defined( DEBUG_SW_TRACE )
    // configure RF Core tracer IO
    IOCPortConfigureSet(IOID_8, IOC_PORT_RFC_TRC, IOC_STD_OUTPUT | IOC_CURRENT_4MA | IOC_SLEW_ENABLE);
  #endif // DEBUG_SW_TRACE
#endif // USE_FPGA

  // Create an RTOS queue for message from profile to be sent to app.
  appMsgQueue = Util_constructQueue(&appMsg);

  //
  uartRxQueue = Util_constructQueue(&uartRxMsg);
  
  // Create one-shot clocks for internal periodic events.
  Util_constructClock(&periodicClock, SimpleBLEPeripheral_clockHandler,
                      SBP_PERIODIC_EVT_PERIOD, SBP_PERIODIC_EVT_PERIOD, true, SBP_PERIODIC_EVT);
  Util_constructClock(&batteryClock, SimpleBLEPeripheral_batteryclockHandler,
                      SBP_VBAT_EVT_PERIOD, 0, false, SBP_VBAT_CHANGE_EVT);
  // Setup the GAP
  GAP_SetParamValue(TGAP_CONN_PAUSE_PERIPHERAL, DEFAULT_CONN_PAUSE_PERIPHERAL);

  // Setup the GAP Peripheral Role Profile
  {
    // For all hardware platforms, device starts advertising upon initialization
    uint8_t initialAdvertEnable = TRUE;

    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    uint16_t advertOffTime = 0;

    uint8_t enableUpdateRequest = DEFAULT_ENABLE_UPDATE_REQUEST;
    uint16_t desiredMinInterval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
    uint16_t desiredMaxInterval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
    uint16_t desiredSlaveLatency = DEFAULT_DESIRED_SLAVE_LATENCY;
    uint16_t desiredConnTimeout = DEFAULT_DESIRED_CONN_TIMEOUT;

    // Set the GAP Role Parameters
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
                         &initialAdvertEnable);
    GAPRole_SetParameter(GAPROLE_ADVERT_OFF_TIME, sizeof(uint16_t),
                         &advertOffTime);

    /*MeasuredPower*/
    osal_snv_read(IB_SNV_MEASUREDPOWER,iBeaconProfile_CHAR1_LEN,&MeasuredPower);
    /*Major*/
    osal_snv_read(IB_SNV_MAJORVALUE,iBeaconProfile_CHAR2_LEN,MajorValue);//lsb,低字节在前
    /*Minor*/
    osal_snv_read(IB_SNV_MINORVALUE,iBeaconProfile_CHAR3_LEN,MinorValue);//lsb,低字节在前
    /*ProximityUUID*/
    osal_snv_read(IB_SNV_IBEACONUUID,iBeaconProfile_CHAR4_LEN,iBeaconUUID);
    /*AdvInterval*/
    osal_snv_read(IB_SNV_ADVINTERVAL,iBeaconProfile_CHAR5_LEN,&AdvInterval);
    
    
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData),
                         scanRspData);
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);

    GAPRole_SetParameter(GAPROLE_PARAM_UPDATE_ENABLE, sizeof(uint8_t),
                         &enableUpdateRequest);
    GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t),
                         &desiredMinInterval);
    GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t),
                         &desiredMaxInterval);
    GAPRole_SetParameter(GAPROLE_SLAVE_LATENCY, sizeof(uint16_t),
                         &desiredSlaveLatency);
    GAPRole_SetParameter(GAPROLE_TIMEOUT_MULTIPLIER, sizeof(uint16_t),
                         &desiredConnTimeout);
  }

  // Set the GAP Characteristics
  GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName);

  // Set advertising interval
  {
    uint16_t advInt = AdvInterval*DEFAULT_ADVERTISING_INTERVAL;

    GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MIN, advInt);
    GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MAX, advInt);
    GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MIN, advInt);
    GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MAX, advInt);
  }

  // Setup the GAP Bond Manager
  {
    uint32_t passkey = 0; // passkey "000000"
    uint8_t pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8_t mitm = TRUE;
    uint8_t ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
    uint8_t bonding = TRUE;

    GAPBondMgr_SetParameter(GAPBOND_DEFAULT_PASSCODE, sizeof(uint32_t),
                            &passkey);
    GAPBondMgr_SetParameter(GAPBOND_PAIRING_MODE, sizeof(uint8_t), &pairMode);
    GAPBondMgr_SetParameter(GAPBOND_MITM_PROTECTION, sizeof(uint8_t), &mitm);
    GAPBondMgr_SetParameter(GAPBOND_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
    GAPBondMgr_SetParameter(GAPBOND_BONDING_ENABLED, sizeof(uint8_t), &bonding);
  }

   // Initialize GATT attributes
  GGS_AddService(GATT_ALL_SERVICES);           // GAP
  GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT attributes
  DevInfo_AddService();                        // Device Information Service

#ifndef FEATURE_OAD_ONCHIP
  iBeaconProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile
#endif //!FEATURE_OAD_ONCHIP

#ifdef FEATURE_OAD
  VOID OAD_addService();                 // OAD Profile
  OAD_register((oadTargetCBs_t *)&simpleBLEPeripheral_oadCBs);
  hOadQ = Util_constructQueue(&oadQ);
#endif //FEATURE_OAD

#ifdef IMAGE_INVALIDATE
  Reset_addService();
#endif //IMAGE_INVALIDATE

  HCI_EXT_SetTxPowerCmd(TxPower);
#ifndef FEATURE_OAD_ONCHIP
  // Setup the SimpleProfile Characteristic Values
  {
    iBeaconProfile_SetParameter( iBeaconProfile_CHAR1, iBeaconProfile_CHAR1_LEN, &MeasuredPower);
    iBeaconProfile_SetParameter( iBeaconProfile_CHAR2, iBeaconProfile_CHAR2_LEN, MajorValue);
    iBeaconProfile_SetParameter( iBeaconProfile_CHAR3, iBeaconProfile_CHAR3_LEN, MinorValue);
    //uuid
    iBeaconProfile_SetParameter( iBeaconProfile_CHAR4, iBeaconProfile_CHAR4_LEN, iBeaconUUID);

    iBeaconProfile_SetParameter( iBeaconProfile_CHAR5, iBeaconProfile_CHAR5_LEN, &AdvInterval);
    iBeaconProfile_SetParameter( iBeaconProfile_CHAR7, iBeaconProfile_CHAR7_LEN, Password);
    iBeaconProfile_SetParameter( iBeaconProfile_CHAR8, iBeaconProfile_CHAR8_LEN, attDeviceName);
    iBeaconProfile_SetParameter( iBeaconProfile_CHAR9, iBeaconProfile_CHAR9_LEN, &TxPower);
  }

  // Register callback with SimpleGATTprofile
  iBeaconProfile_RegisterAppCBs(&SimpleBLEPeripheral_simpleProfileCBs);
#endif //!FEATURE_OAD_ONCHIP

  // Start the Device
  VOID GAPRole_StartDevice(&SimpleBLEPeripheral_gapRoleCBs);

  // Start Bond Manager
  VOID GAPBondMgr_Register(&simpleBLEPeripheral_BondMgrCBs);

  // Register with GAP for HCI/Host messages
  GAP_RegisterForMsgs(selfEntity);

  // Register for GATT local events and ATT Responses pending for transmission
  GATT_RegisterForMsgs(selfEntity);

  HCI_LE_ReadMaxDataLenCmd();

  dispHandle = Display_open(Display_Type_LCD, NULL); //初始化LCD
  
  // UART Task欢迎语
//  TaskUARTdoWrite(NULL, NULL, "%s\r\n", "Beacon");                      //Debug               
//  GY_UartTask_RegisterPacketReceivedCallback(TransUartReceiveDataCallback);
  
  // LED初始化
  HwGPIOInit();
  HwGPIOSet(Board_GLED,1);
  
//  // 初始化从机按键
//  Board_initKeys(SimpleBLEPeripheral_keyChangeHandler);
  
#if defined FEATURE_OAD
#if defined (HAL_IMAGE_A)
  Display_print0(dispHandle, 0, 0, "BLE Peripheral A");
#else
  Display_print0(dispHandle, 0, 0, "BLE Peripheral B");
#endif // HAL_IMAGE_A
#else
  Display_print0(dispHandle, 0, 0, "Beacon");
#endif // FEATURE_OAD
  
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_taskFxn
 *
 * @brief   Application task entry point for the Simple BLE Peripheral.
 *
 * @param   a0, a1 - not used.
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_taskFxn(UArg a0, UArg a1)
{
  // Initialize application
  SimpleBLEPeripheral_init();
  
  // Application main loop
  for (;;)
  {
    // Waits for a signal to the semaphore associated with the calling thread.
    // Note that the semaphore associated with a thread is signaled when a
    // message is queued to the message receive queue of the thread or when
    // ICall_signal() function is called onto the semaphore.
    ICall_Errno errno = ICall_wait(ICALL_TIMEOUT_FOREVER);

    if (errno == ICALL_ERRNO_SUCCESS)
    {
      ICall_EntityID dest;
      ICall_ServiceEnum src;
      ICall_HciExtEvt *pMsg = NULL;

      if (ICall_fetchServiceMsg(&src, &dest,
                                (void **)&pMsg) == ICALL_ERRNO_SUCCESS)
      {
        uint8 safeToDealloc = TRUE;

        if ((src == ICALL_SERVICE_CLASS_BLE) && (dest == selfEntity))
        {
          ICall_Stack_Event *pEvt = (ICall_Stack_Event *)pMsg;

          // Check for BLE stack events first
          if (pEvt->signature == 0xffff)
          {
            if (pEvt->event_flag & SBP_CONN_EVT_END_EVT)
            {
              // Try to retransmit pending ATT Response (if any)
              SimpleBLEPeripheral_sendAttRsp();
            }
          }
          else
          {
            // Process inter-task message
            safeToDealloc = SimpleBLEPeripheral_processStackMsg((ICall_Hdr *)pMsg);
          }
        }

        if (pMsg && safeToDealloc)
        {
          ICall_freeMsg(pMsg);
        }
      }

      // If RTOS queue is not empty, process app message.
      while (!Queue_empty(appMsgQueue))
      {
        sbpEvt_t *pMsg = (sbpEvt_t *)Util_dequeueMsg(appMsgQueue);
        if (pMsg)
        {
          // Process message.
          SimpleBLEPeripheral_processAppMsg(pMsg);

          // Free the space from the message.
          ICall_free(pMsg);
        }
      }
      
      //如果串口RX接收数据队列不为空，处理串口数据 
      while (!Queue_empty(uartRxQueue))                
      {
        uint8_t *pMsg = (uint8_t *)Util_dequeueMsg(uartRxQueue);  //获取队列信号
        if (pMsg)
        {
          Simple_Peripheral_NotiData(pMsg+1,pMsg[0]);   //调用发送函数

          ICall_free(pMsg);
        }
      }
    }

    if (events & SBP_PERIODIC_EVT)
    {
      events &= ~SBP_PERIODIC_EVT;
      VBatCapacity();
      Util_startClock(&batteryClock);
      HwGPIOSet(Board_GLED,0);
    }
    if (events & SBP_VBAT_CHANGE_EVT)
    {
      events &= ~SBP_VBAT_CHANGE_EVT;

      HwGPIOSet(Board_RLED,1);
      DefaultAdvertData();
    }

#ifdef FEATURE_OAD
    while (!Queue_empty(hOadQ))
    {
      oadTargetWrite_t *oadWriteEvt = Queue_get(hOadQ);

      // Identify new image.
      if (oadWriteEvt->event == OAD_WRITE_IDENTIFY_REQ)
      {
        OAD_imgIdentifyWrite(oadWriteEvt->connHandle, oadWriteEvt->pData);
      }
      // Write a next block request.
      else if (oadWriteEvt->event == OAD_WRITE_BLOCK_REQ)
      {
        OAD_imgBlockWrite(oadWriteEvt->connHandle, oadWriteEvt->pData);
      }

      // Free buffer.
      ICall_free(oadWriteEvt);
    }
#endif //FEATURE_OAD
  }
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_processStackMsg
 *
 * @brief   Process an incoming stack message.
 *
 * @param   pMsg - message to process
 *
 * @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
 */
static uint8_t SimpleBLEPeripheral_processStackMsg(ICall_Hdr *pMsg)
{
  uint8_t safeToDealloc = TRUE;

  switch (pMsg->event)
  {
    case GATT_MSG_EVENT:
      // Process GATT message
      safeToDealloc = SimpleBLEPeripheral_processGATTMsg((gattMsgEvent_t *)pMsg);
      break;

    case HCI_GAP_EVENT_EVENT:
      {
        // Process HCI message
        switch(pMsg->status)
        {
          case HCI_COMMAND_COMPLETE_EVENT_CODE:
            // Process HCI Command Complete Event
            break;

          default:
            break;
        }
      }
      break;

    default:
      // do nothing
      break;
  }

  return (safeToDealloc);
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_processGATTMsg
 *
 * @brief   Process GATT messages and events.
 *
 * @return  TRUE if safe to deallocate incoming message, FALSE otherwise.
 */
static uint8_t SimpleBLEPeripheral_processGATTMsg(gattMsgEvent_t *pMsg)
{
  // See if GATT server was unable to transmit an ATT response
  if (pMsg->hdr.status == blePending)
  {
    // No HCI buffer was available. Let's try to retransmit the response
    // on the next connection event.
    if (HCI_EXT_ConnEventNoticeCmd(pMsg->connHandle, selfEntity,
                                   SBP_CONN_EVT_END_EVT) == SUCCESS)
    {
      // First free any pending response
      SimpleBLEPeripheral_freeAttRsp(FAILURE);

      // Hold on to the response message for retransmission
      pAttRsp = pMsg;

      // Don't free the response message yet
      return (FALSE);
    }
  }
  else if (pMsg->method == ATT_FLOW_CTRL_VIOLATED_EVENT)
  {
    // ATT request-response or indication-confirmation flow control is
    // violated. All subsequent ATT requests or indications will be dropped.
    // The app is informed in case it wants to drop the connection.

    // Display the opcode of the message that caused the violation.
    Display_print1(dispHandle, 5, 0, "FC Violated: %d", pMsg->msg.flowCtrlEvt.opcode);
  }
  else if (pMsg->method == ATT_MTU_UPDATED_EVENT)
  {
    // MTU size updated
    Display_print1(dispHandle, 5, 0, "MTU Size: $d", pMsg->msg.mtuEvt.MTU);
  }

  // Free message payload. Needed only for ATT Protocol messages
  GATT_bm_free(&pMsg->msg, pMsg->method);

  // It's safe to free the incoming message
  return (TRUE);
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_sendAttRsp
 *
 * @brief   Send a pending ATT response message.
 *
 * @param   none
 *
 * @return  none
 */
static void SimpleBLEPeripheral_sendAttRsp(void)
{
  // See if there's a pending ATT Response to be transmitted
  if (pAttRsp != NULL)
  {
    uint8_t status;

    // Increment retransmission count
    rspTxRetry++;

    // Try to retransmit ATT response till either we're successful or
    // the ATT Client times out (after 30s) and drops the connection.
    status = GATT_SendRsp(pAttRsp->connHandle, pAttRsp->method, &(pAttRsp->msg));
    if ((status != blePending) && (status != MSG_BUFFER_NOT_AVAIL))
    {
      // Disable connection event end notice
      HCI_EXT_ConnEventNoticeCmd(pAttRsp->connHandle, selfEntity, 0);

      // We're done with the response message
      SimpleBLEPeripheral_freeAttRsp(status);
    }
    else
    {
      // Continue retrying
      Display_print1(dispHandle, 5, 0, "Rsp send retry: %d", rspTxRetry);
    }
  }
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_freeAttRsp
 *
 * @brief   Free ATT response message.
 *
 * @param   status - response transmit status
 *
 * @return  none
 */
static void SimpleBLEPeripheral_freeAttRsp(uint8_t status)
{
  // See if there's a pending ATT response message
  if (pAttRsp != NULL)
  {
    // See if the response was sent out successfully
    if (status == SUCCESS)
    {
      Display_print1(dispHandle, 5, 0, "Rsp sent retry: %d", rspTxRetry);
    }
    else
    {
      // Free response payload
      GATT_bm_free(&pAttRsp->msg, pAttRsp->method);

      Display_print1(dispHandle, 5, 0, "Rsp retry failed: %d", rspTxRetry);
    }

    // Free response message
    ICall_freeMsg(pAttRsp);

    // Reset our globals
    pAttRsp = NULL;
    rspTxRetry = 0;
  }
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_processAppMsg
 *
 * @brief   Process an incoming callback from a profile.
 *
 * @param   pMsg - message to process
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_processAppMsg(sbpEvt_t *pMsg)
{
  switch (pMsg->hdr.event)
  {
    case SBP_STATE_CHANGE_EVT:
      SimpleBLEPeripheral_processStateChangeEvt((gaprole_States_t)pMsg->
                                                hdr.state);
      break;

    case SBP_CHAR_CHANGE_EVT:
      SimpleBLEPeripheral_processCharValueChangeEvt(pMsg->hdr.state);
      break;
      
    case SBP_KEY_CHANGE_EVT:
      SimpleBLEPeripheral_handleKeys(0, pMsg->hdr.state);
      break;
    
    default:
      // Do nothing.
      break;
  }
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_stateChangeCB
 *
 * @brief   Callback from GAP Role indicating a role state change.
 *
 * @param   newState - new state
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_stateChangeCB(gaprole_States_t newState)
{
  SimpleBLEPeripheral_enqueueMsg(SBP_STATE_CHANGE_EVT, newState);
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_processStateChangeEvt
 *
 * @brief   Process a pending GAP Role state change event.
 *
 * @param   newState - new state
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_processStateChangeEvt(gaprole_States_t newState)
{
#ifdef PLUS_BROADCASTER
  static bool firstConnFlag = false;
#endif // PLUS_BROADCASTER

  switch ( newState )
  {
    case GAPROLE_STARTED:
      {
        uint8_t ownAddress[B_ADDR_LEN];
        uint8_t systemId[DEVINFO_SYSTEM_ID_LEN];

        GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddress);

        for(int i=0; i < B_ADDR_LEN; i++)
          macAddr[i] = ownAddress[B_ADDR_LEN-1-i];
        memcpy(scanRspData+IB_SCAN_INDEX_MAC, macAddr, 6);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        
        // use 6 bytes of device address for 8 bytes of system ID value
        systemId[0] = ownAddress[0];
        systemId[1] = ownAddress[1];
        systemId[2] = ownAddress[2];

        // set middle bytes to zero
        systemId[4] = 0x00;
        systemId[3] = 0x00;

        // shift three bytes up
        systemId[7] = ownAddress[5];
        systemId[6] = ownAddress[4];
        systemId[5] = ownAddress[3];

        DevInfo_SetParameter(DEVINFO_SYSTEM_ID, DEVINFO_SYSTEM_ID_LEN, systemId);

        // Display device address
        Display_print0(dispHandle, 1, 0, Util_convertBdAddr2Str(ownAddress));
        Display_print0(dispHandle, 2, 0, "Initialized");
      }
      break;

    case GAPROLE_ADVERTISING:
      Display_print0(dispHandle, 2, 0, "Advertising");
      break;

#ifdef PLUS_BROADCASTER
    /* After a connection is dropped a device in PLUS_BROADCASTER will continue
     * sending non-connectable advertisements and shall sending this change of
     * state to the application.  These are then disabled here so that sending
     * connectable advertisements can resume.
     */
    case GAPROLE_ADVERTISING_NONCONN:
      {
        uint8_t advertEnabled = FALSE;

        // Disable non-connectable advertising.
        GAPRole_SetParameter(GAPROLE_ADV_NONCONN_ENABLED, sizeof(uint8_t),
                           &advertEnabled);

        advertEnabled = TRUE;

        // Enabled connectable advertising.
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
                             &advertEnabled);

        // Reset flag for next connection.
        firstConnFlag = false;

        SimpleBLEPeripheral_freeAttRsp(bleNotConnected);
      }
      break;
#endif //PLUS_BROADCASTER

    case GAPROLE_CONNECTED:
      {
        linkDBInfo_t linkInfo;
        uint8_t numActive = 0;

        Util_startClock(&periodicClock);

        numActive = linkDB_NumActive();

        // Use numActive to determine the connection handle of the last
        // connection
        if ( linkDB_GetInfo( numActive - 1, &linkInfo ) == SUCCESS )
        {
          Display_print1(dispHandle, 2, 0, "Num Conns: %d", (uint16_t)numActive);
          Display_print0(dispHandle, 3, 0, Util_convertBdAddr2Str(linkInfo.addr));
        }
        else
        {
          uint8_t peerAddress[B_ADDR_LEN];

          GAPRole_GetParameter(GAPROLE_CONN_BD_ADDR, peerAddress);

          Display_print0(dispHandle, 2, 0, "Connected");
          Display_print0(dispHandle, 3, 0, Util_convertBdAddr2Str(peerAddress));
        }

        #ifdef PLUS_BROADCASTER
          // Only turn advertising on for this state when we first connect
          // otherwise, when we go from connected_advertising back to this state
          // we will be turning advertising back on.
          if (firstConnFlag == false)
          {
            uint8_t advertEnabled = FALSE; // Turn on Advertising

            // Disable connectable advertising.
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
                                 &advertEnabled);

            // Set to true for non-connectabel advertising.
            advertEnabled = TRUE;

            // Enable non-connectable advertising.
            GAPRole_SetParameter(GAPROLE_ADV_NONCONN_ENABLED, sizeof(uint8_t),
                                 &advertEnabled);
            firstConnFlag = true;
          }
        #endif // PLUS_BROADCASTER
      }
      break;

    case GAPROLE_CONNECTED_ADV:
      Display_print0(dispHandle, 2, 0, "Connected Advertising");
      break;

    case GAPROLE_WAITING:
      Util_stopClock(&periodicClock);
      SimpleBLEPeripheral_freeAttRsp(bleNotConnected);

      Display_print0(dispHandle, 2, 0, "Disconnected");

      // Clear remaining lines
      Display_clearLines(dispHandle, 3, 5);
      break;

    case GAPROLE_WAITING_AFTER_TIMEOUT:
      SimpleBLEPeripheral_freeAttRsp(bleNotConnected);

      Display_print0(dispHandle, 2, 0, "Timed Out");

      // Clear remaining lines
      Display_clearLines(dispHandle, 3, 5);

      #ifdef PLUS_BROADCASTER
        // Reset flag for next connection.
        firstConnFlag = false;
      #endif //#ifdef (PLUS_BROADCASTER)
      break;

    case GAPROLE_ERROR:
      Display_print0(dispHandle, 2, 0, "Error");
      break;

    default:
      Display_clearLine(dispHandle, 2);
      break;
  }

  // Update the state
  //gapProfileState = newState;
}

#ifndef FEATURE_OAD_ONCHIP
/*********************************************************************
 * @fn      SimpleBLEPeripheral_charValueChangeCB
 *
 * @brief   Callback from Simple Profile indicating a characteristic
 *          value change.
 *
 * @param   paramID - parameter ID of the value that was changed.
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_charValueChangeCB(uint8_t paramID)
{
  SimpleBLEPeripheral_enqueueMsg(SBP_CHAR_CHANGE_EVT, paramID);
}
#endif //!FEATURE_OAD_ONCHIP

/*********************************************************************
 * @fn      SimpleBLEPeripheral_processCharValueChangeEvt
 *
 * @brief   Process a pending Simple Profile characteristic value change
 *          event.
 *
 * @param   paramID - parameter ID of the value that was changed.
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_processCharValueChangeEvt(uint8_t paramID)
{
  switch(paramID)
  {
    case iBeaconProfile_CHAR1:
      iBeaconProfile_GetParameter(iBeaconProfile_CHAR1, &MeasuredPower);
      advertData[IB_ADV_INDEX_MEASUREDPOWER] = MeasuredPower;
      GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
      osal_snv_write(IB_SNV_MEASUREDPOWER,iBeaconProfile_CHAR1_LEN,&MeasuredPower);
      break;

    case iBeaconProfile_CHAR2:
      iBeaconProfile_GetParameter(iBeaconProfile_CHAR2, MajorValue);
      memcpy(advertData+IB_ADV_INDEX_MAJOR, MajorValue, 2);
      GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
      memcpy(scanRspData+IB_SCAN_INDEX_MAJOR, MajorValue, 2);
      GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
      osal_snv_write(IB_SNV_MAJORVALUE,iBeaconProfile_CHAR2_LEN,MajorValue);
      break;
	  
	case iBeaconProfile_CHAR3:
      iBeaconProfile_GetParameter(iBeaconProfile_CHAR3, MinorValue);
      memcpy(advertData+IB_ADV_INDEX_MINOR, MinorValue, 2);
      GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
      memcpy(scanRspData+IB_SCAN_INDEX_MINOR, MinorValue, 2);
      GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
      osal_snv_write(IB_SNV_MINORVALUE,iBeaconProfile_CHAR3_LEN,MinorValue);
      break;
	  
      case iBeaconProfile_CHAR4:
      iBeaconProfile_GetParameter(iBeaconProfile_CHAR4, iBeaconUUID);
      memcpy(advertData+IB_ADV_INDEX_UUID, iBeaconUUID, 12);
      GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
      osal_snv_write(IB_SNV_IBEACONUUID,iBeaconProfile_CHAR4_LEN,iBeaconUUID);
      break;

      case iBeaconProfile_CHAR5:
      iBeaconProfile_GetParameter(iBeaconProfile_CHAR5, &AdvInterval);
      GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MIN, AdvInterval*DEFAULT_ADVERTISING_INTERVAL);
      GAP_SetParamValue(TGAP_LIM_DISC_ADV_INT_MAX, AdvInterval*DEFAULT_ADVERTISING_INTERVAL);
      GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MIN, AdvInterval*DEFAULT_ADVERTISING_INTERVAL);
      GAP_SetParamValue(TGAP_GEN_DISC_ADV_INT_MAX, AdvInterval*DEFAULT_ADVERTISING_INTERVAL);
      osal_snv_write(IB_SNV_ADVINTERVAL,iBeaconProfile_CHAR5_LEN,&AdvInterval);
      break;
	  
//	case iBeaconProfile_CHAR6:
//      break;
//
//	case iBeaconProfile_CHAR7:
//      break;
//
//	case iBeaconProfile_CHAR8:
//      break;
//
//	case iBeaconProfile_CHAR9:
//      break;
      
	case iBeaconProfile_CHAR10:
    {
      uint8_t buf[6] = {0,0,0,0,0,0};
      iBeaconProfile_SetParameter( iBeaconProfile_CHAR10, iBeaconProfile_CHAR10_LEN,buf);
    }
      break;

//	case iBeaconProfile_CHAR11:
//      break;
      
    default:
      // should not reach here!
      break;
  }
}
/*********************************************************************
 * @fn      Simple_Peripheral_NotiData
 *
 * @brief   Sends ATT notifications in a tight while loop to demo
 *          throughput
 *
 * @param   none
 *
 * @return  none
 */
void Simple_Peripheral_NotiData(uint8_t *buf, uint16_t len)
{
  // Subtract the total packet overhead of ATT and L2CAP layer from notification payload
  bStatus_t status;
  attHandleValueNoti_t noti;
  noti.handle = 0x27;
  noti.len = len>20 ? 20 : len;

  noti.pValue = (uint8 *)GATT_bm_alloc(0, ATT_HANDLE_VALUE_NOTI, len, NULL);
  if ( noti.pValue != NULL )
  {
    memcpy(noti.pValue, buf, len);
    status = GATT_Notification(0, &noti, FALSE);
    if(status != SUCCESS)
    {
      GATT_bm_free( (gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI );
    }
  }
}

//static void TransUartReceiveDataCallback(uint8_t *buf, uint16_t len)
//{
//  uint8_t *pMsg;
//  // Create dynamic pointer to message.
//  if ((pMsg = ICall_malloc(200)))
//  {
//    pMsg[0] = len;
//    memcpy(pMsg+1, buf, len);
//    // Enqueue the message.
//    Util_enqueueMsg(uartRxQueue, sem, (uint8*)pMsg);
//  }
//}


#ifdef FEATURE_OAD
/*********************************************************************
 * @fn      SimpleBLEPeripheral_processOadWriteCB
 *
 * @brief   Process a write request to the OAD profile.
 *
 * @param   event      - event type:
 *                       OAD_WRITE_IDENTIFY_REQ
 *                       OAD_WRITE_BLOCK_REQ
 * @param   connHandle - the connection Handle this request is from.
 * @param   pData      - pointer to data for processing and/or storing.
 *
 * @return  None.
 */
void SimpleBLEPeripheral_processOadWriteCB(uint8_t event, uint16_t connHandle,
                                           uint8_t *pData)
{
  oadTargetWrite_t *oadWriteEvt = ICall_malloc( sizeof(oadTargetWrite_t) + \
                                             sizeof(uint8_t) * OAD_PACKET_SIZE);

  if ( oadWriteEvt != NULL )
  {
    oadWriteEvt->event = event;
    oadWriteEvt->connHandle = connHandle;

    oadWriteEvt->pData = (uint8_t *)(&oadWriteEvt->pData + 1);
    memcpy(oadWriteEvt->pData, pData, OAD_PACKET_SIZE);

    Queue_put(hOadQ, (Queue_Elem *)oadWriteEvt);

    // Post the application's semaphore.
    Semaphore_post(sem);
  }
  else
  {
    // Fail silently.
  }
}
#endif //FEATURE_OAD

/*********************************************************************
 * @fn      SimpleBLEPeripheral_clockHandler
 *
 * @brief   Handler function for clock timeouts.
 *
 * @param   arg - event type
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_clockHandler(UArg arg)
{
  // Store the event.
  events |= arg;

  // Wake up the application.
  Semaphore_post(sem);
}
/*********************************************************************
 * @fn      SimpleBLEPeripheral_batteryclockHandler
 *
 * @brief   Handler function for clock timeouts.
 *
 * @param   arg - event type
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_batteryclockHandler(UArg a0)
{
  // Store the event.
  events |= SBP_VBAT_CHANGE_EVT;

  // Wake up the application.
  Semaphore_post(sem);
}
/*********************************************************************
 * @fn      SimpleBLEPeripheral_enqueueMsg
 *
 * @brief   Creates a message and puts the message in RTOS queue.
 *
 * @param   event - message event.
 * @param   state - message state.
 *
 * @return  None.
 */
static void SimpleBLEPeripheral_enqueueMsg(uint8_t event, uint8_t state)
{
  sbpEvt_t *pMsg;

  // Create dynamic pointer to message.
  if ((pMsg = ICall_malloc(sizeof(sbpEvt_t))))
  {
    pMsg->hdr.event = event;
    pMsg->hdr.state = state;

    // Enqueue the message.
    Util_enqueueMsg(appMsgQueue, sem, (uint8*)pMsg);
  }
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_keyChangeHandler
 *
 * @brief   Key event handler function
 *
 * @param   a0 - ignored
 *
 * @return  none
 */
void SimpleBLEPeripheral_keyChangeHandler(uint8_t keys)
{
	SimpleBLEPeripheral_enqueueMsg(SBP_KEY_CHANGE_EVT, keys);
}

/*********************************************************************
 * @fn      SimpleBLEPeripheral_handleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   shift - true if in shift/alt.
 * @param   keys - bit field for key events. Valid entries:
 *                 HAL_KEY_SW_2
 *                 HAL_KEY_SW_1
 *
 * @return  none
 */
static void SimpleBLEPeripheral_handleKeys(uint8_t shift, uint8_t keys)
{
  if (keys & KEY_BTN1)
  {

  }
  
  if (keys & KEY_BTN2)
  {
 
  }
	return;
}

/*********************************************************************
 * @fn      RestoreFactorySettings
 *
 * @brief   .
 *
 * @param   none.
 *
 * @return  none
 */
void RestoreFactorySettings(void)
{
  MeasuredPower = MeasuredPower_Factory;
  osal_snv_write(IB_SNV_MEASUREDPOWER,iBeaconProfile_CHAR1_LEN,&MeasuredPower);

  memcpy(MajorValue,MajorValue_Factory,iBeaconProfile_CHAR2_LEN);
  osal_snv_write(IB_SNV_MAJORVALUE,iBeaconProfile_CHAR2_LEN,MajorValue);//lsb,低字节在前

  memcpy(MinorValue,MinorValue_Factory,iBeaconProfile_CHAR3_LEN);
  osal_snv_write(IB_SNV_MINORVALUE,iBeaconProfile_CHAR3_LEN,MinorValue);//lsb,低字节在前

  memcpy(iBeaconUUID,iBeaconUUID_Factory,iBeaconProfile_CHAR4_LEN);
  osal_snv_write(IB_SNV_IBEACONUUID,iBeaconProfile_CHAR4_LEN,iBeaconUUID);//lsb,低字节在前
  
  AdvInterval = AdvInterval_Factory;
  osal_snv_write(IB_SNV_ADVINTERVAL,iBeaconProfile_CHAR5_LEN,&AdvInterval);
}
void VBatCapacity(void)
{
    advertData[25] = 0x5A;
    advertData[26] = 0x5A;
    advertData[27] = 0x00;
    advertData[28] = 0x00;
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
}
void DefaultAdvertData(void)
{
    advertData[25] = 0x55;
    advertData[26] = 0x00;
    advertData[27] = 0x00;
    advertData[28] = 0x00;
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
}
/*********************************************************************
*********************************************************************/
