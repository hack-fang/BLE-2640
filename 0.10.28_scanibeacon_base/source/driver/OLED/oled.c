#ifdef SCANBEACON_ADVANCE

#include "oled.h"
#include "board.h"
#include "hw_gpio.h"
#include "oledfont.h"

void OLED_Delay(uint32_t times)
{
    while(times > 0) {
        times --;
    }
}
//置位RESET

static inline void OLED_SetRes()
{
    HwGPIOSet(OLED_SPI_RESET, 1);
}
//清除RESET
static inline void OLED_ClearRes()
{
    HwGPIOSet(OLED_SPI_RESET, 0);
}
//向SSD1106写入一个字节
void OLED_WR_Byte(uint8_t data,uint8_t cmd)
{
    uint8_t i;
    HwGPIOSet(OLED_SPI_CS,0);
    NOP(6);
    HwGPIOSet(Board_SPI0_CLK,0);
    NOP(6);
    if(cmd)
      HwGPIOSet(Board_SPI0_MOSI,1);
    else
      HwGPIOSet(Board_SPI0_MOSI,0);
    NOP(6);
    HwGPIOSet(Board_SPI0_CLK,1);
    NOP(6);
    for(i=0;i<8;i++)
    {
      HwGPIOSet(Board_SPI0_CLK,0);
      NOP(6);
      if(data&0x80)
        HwGPIOSet(Board_SPI0_MOSI,1);
      else
        HwGPIOSet(Board_SPI0_MOSI,0);
      NOP(6);
      HwGPIOSet(Board_SPI0_CLK,1);
      data<<=1;
      NOP(6);
    }
    HwGPIOSet(OLED_SPI_CS,1);
}

//设置坐标
void OLED_Set_Pos(uint8_t x,uint8_t y)
{
    OLED_WR_Byte(0xb0+y,OLED_CMD);
    OLED_WR_Byte(((x&0xf0)>>4)|0x10,OLED_CMD);
    OLED_WR_Byte((x&0x0f)|0x01,OLED_CMD);
}
//开启OLED显示
void OLED_Display_On(void)
{
    OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC命令
    OLED_WR_Byte(0X14,OLED_CMD);  //DCDC ON
    OLED_WR_Byte(0XAF,OLED_CMD);  //DISPLAY ON
}
//关闭OLED显示    
void OLED_Display_Off(void)
{
    OLED_WR_Byte(0X8D,OLED_CMD);  //SET DCDC命令
    OLED_WR_Byte(0X10,OLED_CMD);  //DCDC OFF
    OLED_WR_Byte(0XAE,OLED_CMD);  //DISPLAY OFF
}
//清屏	  
void OLED_Clear(void)  
{  
	uint8_t i,n;		    
	for(i=0;i<8;i++)  
	{  
            OLED_WR_Byte (0xb0+i,OLED_CMD);    //设置页地址（0-7）
            OLED_WR_Byte (0x00,OLED_CMD);      //设置页显示位置-列低地址
            OLED_WR_Byte (0x10,OLED_CMD);      //设置页显示位置-列高地址  
            for(n=0;n<128;n++)
              OLED_WR_Byte(0,OLED_DATA); 
	} //更新显示
}
//在指定位置显示一个字符
//x:0~127
//y:0~63
//mode:0,反白显示;1,正常显示				 
//size:选择字体 16/12 
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr)
{      	
    unsigned char c=0,i=0;	
    c=chr-' ';//得到偏移后的值		
    if(x>Max_Column-1){x=0;y=y+2;}
    if(OLED_SIZE ==16)
      {
      OLED_Set_Pos(x,y);	
      for(i=0;i<8;i++)
      OLED_WR_Byte(F8X16[c*16+i],OLED_DATA);
      OLED_Set_Pos(x,y+1);
      for(i=0;i<8;i++)
      OLED_WR_Byte(F8X16[c*16+i+8],OLED_DATA);
      }
      else 
      {	
          OLED_Set_Pos(x,y+1);
          for(i=0;i<6;i++)
          OLED_WR_Byte(F6x8[c][i],OLED_DATA);     
      }
}
//显示字符串
void OLED_ShowString(uint8_t x,uint8_t y,uint8_t *chr)
{
	unsigned char j=0;
	while (chr[j]!='\0')
	{		OLED_ShowChar(x,y,chr[j]);
			x+=8;
		if(x>120){x=0;y+=2;}
			j++;
	}
}
//显示汉字
//void OLED_ShowCHinese(uint8_t x,uint8_t y,uint8_t no)
//{      			    
//	uint8_t t,adder=0;
//	OLED_Set_Pos(x,y);	
//    for(t=0;t<16;t++)
//		{
//				OLED_WR_Byte(Hzk[2*no][t],OLED_DATA);
//				adder+=1;
//     }	
//		OLED_Set_Pos(x,y+1);	
//    for(t=0;t<16;t++)
//			{	
//				OLED_WR_Byte(Hzk[2*no+1][t],OLED_DATA);
//				adder+=1;
//      }					
//}

//m^n 函数
uint32_t oled_pow(uint8_t m,uint8_t n)
{
	uint32_t result=1;	 
	while(n--)result *= m;    
	return result;
}
//显示两个数字
//x,y :起点坐标 
//len :数字的位数
//size:字体大小
//mode:模式0	0,填充模式;1,叠加模式
//num:数值(0~4294967295);	 		  
void OLED_ShowNum(uint8_t x,uint8_t y,uint32_t num,uint8_t len,uint8_t size)
{         	
	uint8_t t,temp;
	uint8_t enshow=0;						   
	for(t=0;t<len;t++)
	{
		temp=(num/oled_pow(10,len-t-1))%10;
		if(enshow==0&&t<(len-1))
		{
			if(temp==0)
			{
				OLED_ShowChar(x+(size/2)*t,y,' ');
				continue;
			}else enshow=1; 
		 	 
		}
	 	OLED_ShowChar(x+(size/2)*t,y,temp+'0'); 
	}
} 
void OLED_Init(void)
{
    HwGPIOSet(OLED_POWER_EN,0);
    /* Reset OLED and Waitting for Vcc stable */
    OLED_SetRes();
    OLED_Delay(100);
    OLED_ClearRes();
    OLED_Delay(100);
    OLED_SetRes();
    
    OLED_WR_Byte(0xAE,OLED_CMD);//--turn off oled panel
    OLED_WR_Byte(0x00,OLED_CMD);//---set low column address
    OLED_WR_Byte(0x10,OLED_CMD);//---set high column address
    OLED_WR_Byte(0x40,OLED_CMD);//--set start line address  Set Mapping RAM Display Start Line (0x00~0x3F)
    OLED_WR_Byte(0x81,OLED_CMD);//--set contrast control register
    OLED_WR_Byte(0xCF,OLED_CMD); // Set SEG Output Current Brightness
    OLED_WR_Byte(0xA1,OLED_CMD);//--Set SEG/Column Mapping     0xa0×óóò·′?? 0xa1?y3￡
    OLED_WR_Byte(0xC8,OLED_CMD);//Set COM/Row Scan Direction   0xc0é???·′?? 0xc8?y3￡
    OLED_WR_Byte(0xA6,OLED_CMD);//--set normal display
    OLED_WR_Byte(0xA8,OLED_CMD);//--set multiplex ratio(1 to 64)
    OLED_WR_Byte(0x3f,OLED_CMD);//--1/64 duty
    OLED_WR_Byte(0xD3,OLED_CMD);//-set display offset	Shift Mapping RAM Counter (0x00~0x3F)
    OLED_WR_Byte(0x00,OLED_CMD);//-not offset
    OLED_WR_Byte(0xd5,OLED_CMD);//--set display clock divide ratio/oscillator frequency
    OLED_WR_Byte(0x80,OLED_CMD);//--set divide ratio, Set Clock as 100 Frames/Sec
    OLED_WR_Byte(0xD9,OLED_CMD);//--set pre-charge period
    OLED_WR_Byte(0xF1,OLED_CMD);//Set Pre-Charge as 15 Clocks & Discharge as 1 Clock
    OLED_WR_Byte(0xDA,OLED_CMD);//--set com pins hardware configuration
    OLED_WR_Byte(0x12,OLED_CMD);
    OLED_WR_Byte(0xDB,OLED_CMD);//--set vcomh
    OLED_WR_Byte(0x40,OLED_CMD);//Set VCOM Deselect Level
    OLED_WR_Byte(0x20,OLED_CMD);//-Set Page Addressing Mode (0x00/0x01/0x02)
    OLED_WR_Byte(0x02,OLED_CMD);//
    OLED_WR_Byte(0x8D,OLED_CMD);//--set Charge Pump enable/disable
    OLED_WR_Byte(0x14,OLED_CMD);//--set(0x10) disable
    OLED_WR_Byte(0xA4,OLED_CMD);// Disable Entire Display On (0xa4/0xa5)
    OLED_WR_Byte(0xA6,OLED_CMD);// Disable Inverse Display On (0xa6/a7) 
    OLED_WR_Byte(0xAF,OLED_CMD);//--turn on oled panel

    OLED_WR_Byte(0xAF,OLED_CMD); /*display ON*/ 
    OLED_Clear();
    OLED_Set_Pos(0,0);
}
#endif