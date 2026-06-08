#include "oled.h"
#include "oled_font.h"
// 屏幕缓存
static uint8_t OLED_GRAM[128][8];

// 软件SPI发送一个字节
static void OLED_SPI_WriteByte(uint8_t dat)
{
    uint8_t i;
    OLED_CLK_LOW;
    for(i=0;i<8;i++)
    {
        // 高位在前
        if(dat & 0x80)
            OLED_MOSI_HIGH;
        else
            OLED_MOSI_LOW;
        
        // 时钟线拉高，写入数据
        OLED_CLK_HIGH;
        dat <<= 1;
        // 时钟线拉低，准备下一位
        OLED_CLK_LOW;
    }
}


// 发送命令
static void OLED_WriteCommand(uint8_t cmd)
{
    OLED_CS_LOW;    // 选中OLED
    OLED_DC_LOW;    // 命令模式
    OLED_SPI_WriteByte(cmd);
    OLED_CS_HIGH;   // 取消选中
}

// 发送数据
static void OLED_WriteData(uint8_t data)
{
    OLED_CS_LOW;    // 选中OLED
    OLED_DC_HIGH;   // 数据模式
    OLED_SPI_WriteByte(data);
    OLED_CS_HIGH;   // 取消选中
}

// 初始化OLED
void OLED_Init(void)
{
    // 延时，确保电源稳定
    HAL_Delay(100);
    
    // 复位OLED
    OLED_RES_LOW;
    HAL_Delay(100);
    OLED_RES_HIGH;
    
    // 初始化序列(SSD1306控制器)
    OLED_WriteCommand(0xAE); // 关闭显示
    
    OLED_WriteCommand(0xD5); // 设置时钟分频因子,震荡频率
    OLED_WriteCommand(0x80); // 建议值0x80
    
    OLED_WriteCommand(0xA8); // 设置多路复用率
    OLED_WriteCommand(0x3F); // 0x3F 1/64 duty
    
    OLED_WriteCommand(0xD3); // 设置显示偏移
    OLED_WriteCommand(0x00); // 无偏移
    
    OLED_WriteCommand(0x40); // 设置显示开始行 [5:0],行数.
    
    OLED_WriteCommand(0x8D); // 电荷泵设置
    OLED_WriteCommand(0x14); // bit2，开启/关闭
    
    OLED_WriteCommand(0x20); // 设置内存地址模式
    OLED_WriteCommand(0x00); // 00，列地址模式；01，行地址模式；10，页地址模式；默认10；
    
    OLED_WriteCommand(0xA1); // 段重定向设置,bit0:0,0->0;1,0->127;
    
    OLED_WriteCommand(0xC8); // 设置COM扫描方向;bit3:0,普通模式;1,重定义模式 COM[N-1]->COM0;N:驱动路数
    
    OLED_WriteCommand(0xDA); // 设置COM硬件引脚配置
    OLED_WriteCommand(0x12); // [5:4]配置
    
    OLED_WriteCommand(0x81); // 对比度设置
    OLED_WriteCommand(0xCF); // 1~255;默认0x7F (亮度设置,越大越亮)
    
    OLED_WriteCommand(0xD9); // 设置预充电周期
    OLED_WriteCommand(0xF1); // [3:0],PHASE 1;[7:4],PHASE 2;
    
    OLED_WriteCommand(0xDB); // 设置VCOMH 电压倍率
    OLED_WriteCommand(0x40); // [6:4] 000,0.65*Vcc;001,0.77*Vcc;011,0.83*Vcc;
    
    OLED_WriteCommand(0xA4); // 全局显示开启;bit0:1,开启;0,关闭;(白屏/黑屏)
    
    OLED_WriteCommand(0xA6); // 设置显示方式;bit0:1,反相显示;0,正常显示
    
    OLED_WriteCommand(0xAF); // 开启显示
    
    OLED_Clear(); // 清屏
}

// 清屏
void OLED_Clear(void)
{
    memset(OLED_GRAM, 0, sizeof(OLED_GRAM));
    OLED_UpdateScreen();
}

// 更新屏幕显示
void OLED_UpdateScreen(void)
{
    uint8_t i, n;
    for(i=0; i<8; i++)
    {
        OLED_WriteCommand(0xB0 + i); // 设置页地址
        OLED_WriteCommand(0x00);     // 设置列地址低4位
        OLED_WriteCommand(0x10);     // 设置列地址高4位
        
        for(n=0; n<128; n++)
        {
            OLED_WriteData(OLED_GRAM[n][i]);
        }
    }
}

// 在指定位置显示一个字符
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 size1)
{
    u8 i,m,temp,size2,chr1;
	u8 x0=x,y0=y;
	if(size1==8)size2=6;
	else size2=(size1/8+((size1%8)?1:0))*(size1/2);  //µÃµ½×ÖÌåÒ»¸ö×Ö·û¶ÔÓ¦µãÕó¼¯ËùÕ¼µÄ×Ö½ÚÊý
	chr1=chr-' ';  //¼ÆËãÆ«ÒÆºóµÄÖµ
	for(i=0;i<size2;i++)
	{
		if(size1==8)
			  {temp=asc2_0806[chr1][i];} //µ÷ÓÃ0806×ÖÌå
		else if(size1==12)
        {temp=asc2_1206[chr1][i];} //µ÷ÓÃ1206×ÖÌå
		else if(size1==16)
        {temp=asc2_1608[chr1][i];} //µ÷ÓÃ1608×ÖÌå
		else if(size1==24)
        {temp=asc2_2412[chr1][i];} //µ÷ÓÃ2412×ÖÌå
		else return;
		for(m=0;m<8;m++)
		{
			if(temp&0x01)OLED_DrawPoint(x,y,1);
			else OLED_DrawPoint(x,y,0);
			temp>>=1;
			y++;
		}
		x++;
		if((size1!=8)&&((x-x0)==size1/2))
		{x=x0;y0=y0+8;}
		y=y0;
  }
}

//ÏÔÊ¾×Ö·û´®
//x,y:Æðµã×ø±ê  
//size1:×ÖÌå´óÐ¡ 
//*chr:×Ö·û´®ÆðÊ¼µØÖ· 
//mode:0,·´É«ÏÔÊ¾;1,Õý³£ÏÔÊ¾
void OLED_ShowString(u8 x,u8 y,u8 *chr,u8 size1)
{
	while((*chr>=' ')&&(*chr<='~'))//ÅÐ¶ÏÊÇ²»ÊÇ·Ç·¨×Ö·û!
	{
		OLED_ShowChar(x,y,*chr,size1);
		if(size1==8)x+=6;
		else x+=size1/2;
		chr++;
  }
}

// 画点
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t mode)
{
    uint8_t page, bit;
    
    if(x > 127 || y > 63) return; // 超出范围
    
    page = y / 8;
    bit = y % 8;
    
    if(mode)
        OLED_GRAM[x][page] |= (1 << bit);
    else
        OLED_GRAM[x][page] &= ~(1 << bit);
}

//»­Ïß
//x1,y1:Æðµã×ø±ê
//x2,y2:½áÊø×ø±ê
void OLED_DrawLine(u8 x1,u8 y1,u8 x2,u8 y2)
{
	u16 t; 
	int xerr=0,yerr=0,delta_x,delta_y,distance;
	int incx,incy,uRow,uCol;
	delta_x=x2-x1; //¼ÆËã×ø±êÔöÁ¿ 
	delta_y=y2-y1;
	uRow=x1;//»­ÏßÆðµã×ø±ê
	uCol=y1;
	if(delta_x>0)incx=1; //ÉèÖÃµ¥²½·½Ïò 
	else if (delta_x==0)incx=0;//´¹Ö±Ïß 
	else {incx=-1;delta_x=-delta_x;}
	if(delta_y>0)incy=1;
	else if (delta_y==0)incy=0;//Ë®Æ½Ïß 
	else {incy=-1;delta_y=-delta_x;}
	if(delta_x>delta_y)distance=delta_x; //Ñ¡È¡»ù±¾ÔöÁ¿×ø±êÖá 
	else distance=delta_y;
	for(t=0;t<distance+1;t++)
	{
		OLED_DrawPoint(uRow,uCol,1);//»­µã
		xerr+=delta_x;
		yerr+=delta_y;
		if(xerr>distance)
		{
			xerr-=distance;
			uRow+=incx;
		}
		if(yerr>distance)
		{
			yerr-=distance;
			uCol+=incy;
		}
	}
}
//x,y:Ô²ÐÄ×ø±ê
//r:Ô²µÄ°ë¾¶
void OLED_DrawCircle(u8 x,u8 y,u8 r)
{
	int a, b,num;
    a = 0;
    b = r;
    while(2 * b * b >= r * r)      
    {
        OLED_DrawPoint(x + a, y - b,1);
        OLED_DrawPoint(x - a, y - b,1);
        OLED_DrawPoint(x - a, y + b,1);
        OLED_DrawPoint(x + a, y + b,1);
 
        OLED_DrawPoint(x + b, y + a,1);
        OLED_DrawPoint(x + b, y - a,1);
        OLED_DrawPoint(x - b, y - a,1);
        OLED_DrawPoint(x - b, y + a,1);
        
        a++;
        num = (a * a + b * b) - r*r;//¼ÆËã»­µÄµãÀëÔ²ÐÄµÄ¾àÀë
        if(num > 0)
        {
            b--;
            a--;
        }
    }
}
//m^n
u32 OLED_Pow(u8 m,u8 n)
{
	u32 result=1;
	while(n--)
	{
	  result*=m;
	}
	return result;
}

//ÏÔÊ¾Êý×Ö
//x,y :Æðµã×ø±ê
//num :ÒªÏÔÊ¾µÄÊý×Ö
//len :Êý×ÖµÄÎ»Êý
//size:×ÖÌå´óÐ¡
//mode:0,·´É«ÏÔÊ¾;1,Õý³£ÏÔÊ¾
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size1)
{
	u8 t,temp,m=0;
	if(size1==8)m=2;
	for(t=0;t<len;t++)
	{
		temp=(num/OLED_Pow(10,len-t-1))%10;
			if(temp==0)
			{
				OLED_ShowChar(x+(size1/2+m)*t,y,'0',size1);
      }
			else 
			{
			  OLED_ShowChar(x+(size1/2+m)*t,y,temp+'0',size1);
			}
  }
}

//ÏÔÊ¾ºº×Ö
//x,y:Æðµã×ø±ê
//num:ºº×Ö¶ÔÓ¦µÄÐòºÅ
//mode:0,·´É«ÏÔÊ¾;1,Õý³£ÏÔÊ¾
void OLED_ShowChinese(u8 x,u8 y,u8 num,u8 size1)
{
	u8 m,temp;
	u8 x0=x,y0=y;
	u16 i,size3=(size1/8+((size1%8)?1:0))*size1;  //µÃµ½×ÖÌåÒ»¸ö×Ö·û¶ÔÓ¦µãÕó¼¯ËùÕ¼µÄ×Ö½ÚÊý
	for(i=0;i<size3;i++)
	{
		if(size1==16)
				{temp=Hzk1[num][i];}//µ÷ÓÃ16*16×ÖÌå
		else if(size1==24)
				{temp=Hzk2[num][i];}//µ÷ÓÃ24*24×ÖÌå
		else if(size1==32)       
				{temp=Hzk3[num][i];}//µ÷ÓÃ32*32×ÖÌå
		else if(size1==64)
				{temp=Hzk4[num][i];}//µ÷ÓÃ64*64×ÖÌå
		else return;
		for(m=0;m<8;m++)
		{
			if(temp&0x01)OLED_DrawPoint(x,y,1);
			else OLED_DrawPoint(x,y,0);
			temp>>=1;
			y++;
		}
		x++;
		if((x-x0)==size1)
		{x=x0;y0=y0+8;}
		y=y0;
	}
}

//num ÏÔÊ¾ºº×ÖµÄ¸öÊý
//space Ã¿Ò»±éÏÔÊ¾µÄ¼ä¸ô
//mode:0,·´É«ÏÔÊ¾;1,Õý³£ÏÔÊ¾
void OLED_ScrollDisplay(u8 num,u8 space)
{
	u8 i,n,t=0,m=0,r;
	while(1)
	{
		if(m==0)
		{
	    OLED_ShowChinese(128,24,t,16); //Ð´ÈëÒ»¸öºº×Ö±£´æÔÚOLED_GRAM[][]Êý×éÖÐ
			t++;
		}
		if(t==num)
			{
				for(r=0;r<16*space;r++)      //ÏÔÊ¾¼ä¸ô
				 {
					for(i=1;i<144;i++)
						{
							for(n=0;n<8;n++)
							{
								OLED_GRAM[i-1][n]=OLED_GRAM[i][n];
							}
						}
           OLED_UpdateScreen();
				 }
        t=0;
      }
		m++;
		if(m==16){m=0;}
		for(i=1;i<144;i++)   //ÊµÏÖ×óÒÆ
		{
			for(n=0;n<8;n++)
			{
				OLED_GRAM[i-1][n]=OLED_GRAM[i][n];
			}
		}
		OLED_UpdateScreen();
	}
}

//x,y£ºÆðµã×ø±ê
//sizex,sizey,Í¼Æ¬³¤¿í
//BMP[]£ºÒªÐ´ÈëµÄÍ¼Æ¬Êý×é
//mode:0,·´É«ÏÔÊ¾;1,Õý³£ÏÔÊ¾
void OLED_ShowPicture(u8 x,u8 y,u8 sizex,u8 sizey,u8 BMP[])
{
	u16 j=0;
	u8 i,n,temp,m;
	u8 x0=x,y0=y;
	sizey=sizey/8+((sizey%8)?1:0);
	for(n=0;n<sizey;n++)
	{
		 for(i=0;i<sizex;i++)
		 {
				temp=BMP[j];
				j++;
				for(m=0;m<8;m++)
				{
					if(temp&0x01)OLED_DrawPoint(x,y,1);
					else OLED_DrawPoint(x,y,0);
					temp>>=1;
					y++;
				}
				x++;
				if((x-x0)==sizex)
				{
					x=x0;
					y0=y0+8;
				}
				y=y0;
     }
	 }
}
