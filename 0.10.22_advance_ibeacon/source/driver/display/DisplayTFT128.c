/*
 *             Copyright (c) 2017, Ghostyu Co.,Ltd.
 *                      All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* -----------------------------------------------------------------------------
 *  Includes
 * -----------------------------------------------------------------------------
 */
// TI RTOS drivers
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/SPI.h>

#include <xdc/runtime/Log.h>
#include <xdc/runtime/System.h>

#include <ti/mw/display/Display.h>
#include <DisplayTFT128.h>
#include <TFT128.h>
 
/* -----------------------------------------------------------------------------
 *  Constants and macros
 * -----------------------------------------------------------------------------
 */
// Timeout of semaphore that controls exclusive to the LCD (500 ms)
#define ACCESS_TIMEOUT    50000

/* -----------------------------------------------------------------------------
 *   Type definitions
 * -----------------------------------------------------------------------------
 */


/* -----------------------------------------------------------------------------
 *                           Local variables
 * -----------------------------------------------------------------------------
 */
/* Display function table for tft1.44 128x128 implementation */
const Display_FxnTable DisplayTFT128_fxnTable = {
    DisplayTFT128_open,
    DisplayTFT128_clear,
    DisplayTFT128_clearLines,
    DisplayTFT128_put5,
    DisplayTFT128_close,
    DisplayTFT128_control,
    DisplayTFT128_getType,
};

/* -----------------------------------------------------------------------------
 *                                          Functions
 * -----------------------------------------------------------------------------
 */
//******************************************************************************
// fn :         DisplayTFT128_open
//
// brief :      Initialize the LCD
//
// descr :     Initializes the pins used by the LCD, creates resource access
//              protection semaphore, turns on the LCD device, initializes the
//              frame buffer, initializes to white background/dark foreground,
//              and finally clears the object->displayColor.
//
// param :     hDisplay - pointer to Display_Config struct
//             params - display parameters
//
// return :    Pointer to Display_Config struct
Display_Handle DisplayTFT128_open(Display_Handle hDisplay,
                                    Display_Params *params)
{
    DisplayTFT128_HWAttrs *hwAttrs = (DisplayTFT128_HWAttrs *)hDisplay->hwAttrs;
    DisplayTFT128_Object  *object  = (DisplayTFT128_Object  *)hDisplay->object;

    PIN_Config pinTable[1 + 1];

    uint32_t   i = 0;
    if (hwAttrs->powerPin != PIN_TERMINATE)
    {
        pinTable[i++] = hwAttrs->powerPin | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL | PIN_DRVSTR_MAX;
    }
    pinTable[i++] = PIN_TERMINATE;

    object->hPins = PIN_open(&object->pinState, pinTable);
    if (object->hPins == NULL)
    {
        Log_error0("Couldn't open pins for TFT128_128");
        return NULL;
    }

    object->lineClearMode = params->lineClearMode;
    object->lcdColor.blackColor = BLACK;
    object->lcdColor.frontColor = WHITE;

    object->lcdBuffers[0].pcBuffer = object->lcdBuffer0;
    object->lcdBuffers[0].bufSize  = 512;
    object->lcdBuffers[0].bufMutex = object->lcdMutex;
    
    
    LCD_Params lcdParams;
    LCD_Params_init(&lcdParams);
    object->hLcd = LCD_open(&object->lcdBuffers[0], 1, &lcdParams);
    object->hLcd->object->pColorInfo = &object->lcdColor;
    
    if (object->hLcd)
    {
        LCD_bufferClear(object->hLcd);
        return hDisplay;
    }
    else
    {
        PIN_close(object->hPins);
        return NULL;
    }
}
//******************************************************************************
// fn :         DisplayTFT128_clear
//
// brief :      Clears the display
//
// param :       hDisplay - pointer to Display_Config struct
//
// return  :    void
void DisplayTFT128_clear(Display_Handle hDisplay)
{
    DisplayTFT128_Object *object = (DisplayTFT128_Object  *)hDisplay->object;

    if (object->hLcd)
    {
        LCD_bufferClear(object->hLcd);
    }
}
//******************************************************************************
// fn :         DisplayTFT128_clearLines
//
// brief :      Clears lines lineFrom-lineTo of the display, inclusive
//
// param :      hDisplay - pointer to Display_Config struct
//              lineFrom - line index (0 .. )
//              lineTo - line index (0 .. )
//
// return :    void
void DisplayTFT128_clearLines(Display_Handle hDisplay,
                                uint8_t lineFrom, uint8_t lineTo)
{
    DisplayTFT128_Object *object = (DisplayTFT128_Object  *)hDisplay->object;

    if (lineTo < lineFrom)
    {
        lineTo = lineFrom;
    }

    if (object->hLcd)
    {
        uint8_t xMin = 0;
        uint8_t xMax = 127;

        LCD_bufferClearPart(object->hLcd, xMin, xMax,
                            (LCD_Page)lineFrom, (LCD_Page)lineTo);
    }
}


//******************************************************************************
// fn :         DisplayTFT128_put5
//
// brief :     Write a text string to a specific line/column of the display
//
// param :     hDisplay - pointer to Display_Config struct
//             line - line index (0..)
//             column - column index (0..)
//             fmt - format string
//             aN - optional format arguments
//
// return :    void
void DisplayTFT128_put5(Display_Handle hDisplay, uint8_t line,
                          uint8_t column, uintptr_t fmt, uintptr_t a0,
                          uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4)
{
    DisplayTFT128_Object *object = (DisplayTFT128_Object  *)hDisplay->object;

    char    dispStr[22] = { 0 };
    uint8_t xp, clearStartX, clearEndX;

    xp          = column * 8;   //ascii =  16 * 8
    clearStartX = clearEndX = xp;

    switch (object->lineClearMode)
    {
    case DISPLAY_CLEAR_LEFT:
        clearStartX = 0;
        break;
    case DISPLAY_CLEAR_RIGHT:
        clearEndX = 127;
        break;
    case DISPLAY_CLEAR_BOTH:
        clearStartX = 0;
        clearEndX   = 127;
        break;
    case DISPLAY_CLEAR_NONE:
    default:
        break;
    }

    if (clearStartX != clearEndX)
    {
        LCD_bufferClearPart(object->hLcd,
                            clearStartX, clearEndX, (LCD_Page)line, (LCD_Page)(line));
    }


    System_snprintf(dispStr, sizeof(dispStr), (xdc_CString)fmt, a0, a1, a2, a3, a4);

    LCD_bufferPrintString(object->hLcd, dispStr, xp, (LCD_Page)line);
}


//******************************************************************************
// fn :         DisplayTFT128_close
//
// brief:       Turns of the display and releases the LCD control pins
//
// param :      hDisplay - pointer to Display_Config struct
//
// return :     void
void DisplayTFT128_close(Display_Handle hDisplay)
{
    DisplayTFT128_Object *object = (DisplayTFT128_Object  *)hDisplay->object;

    if (object->hPins == NULL)
    {
        return;
    }

    // Turn off the display
    PIN_close(object->hPins);
    object->hPins = NULL;

    LCD_close(object->hLcd);
    object->hLcd = NULL;
}

//******************************************************************************
// fn :      DisplayTFT128_control
//
// brief :   Function for setting control parameters of the Display driver
//           after it has been opened.
//
// param :   hDisplay - pointer to Display_Config struct
//           cmd - command to execute
//           arg - argument to the command
//
// return : DISPLAY_STATUS_UNDEFINEDCMD because no commands are supported
int DisplayTFT128_control(Display_Handle handle, unsigned int cmd, void *arg)
{
  DisplayTFT128_Object *object = (DisplayTFT128_Object  *)handle->object;
  
  if(CMD_BLACK_COLOR == cmd)
  {
    object->lcdColor.blackColor = (Color)(*((int*)arg));
  }
  else if(CMD_FRONT_COLOR == cmd)
  {
    object->lcdColor.frontColor = (Color)(*((int*)arg));
  }
  return DISPLAY_STATUS_UNDEFINEDCMD;
}

//******************************************************************************
// fn :         DisplayTFT128_getType
//
// brief :      Returns type of transport
//
// param :
//
// return :     Display type define LCD
unsigned int DisplayTFT128_getType(void)
{
    return Display_Type_LCD;
}
