/************************************* 
*filename：serialconfig.c 
*author:ZhenjunLiu 
*desc:config the serial with some args 
*************************************/  
  
#include "uart.h"  
 
int set_uart_config(int fd,int baudrate,int data_bit,char parity,int stop_bit)  
{  
        struct termios new_cfg,old_cfg;  
        int speed;  
          
        /*1.保存原先串口配置*/  
        if(tcgetattr(fd,&old_cfg) != 0)  
        {  
                perror("tcgetattr");  
                return -1;  
                  
        }  
                      
        new_cfg = old_cfg;  
          
          
        /*2.激活选项,设置为原始模式*/  
        new_cfg.c_cflag |= CLOCAL | CREAD;  
        cfmakeraw(&new_cfg);  
          
        /*3.设置位掩码，除去位掩码*/  
        new_cfg.c_cflag &= ~CSIZE;  
          
          
        /*4.设置波特率*/  
        switch(baudrate)  
        {  
                case 2400:  
                    {  
                        speed = B2400;    
                    }  
                    break;  
                      
                case 4800:  
                    {  
                        speed = B4800;    
                    }  
                    break;  
                case 9600:  
                    {  
                        speed = B9600;    
                    }  
                    break;  
                case 19200:  
                    {  
                        speed = B19200;   
                    }  
                    break;  
                case 38400:  
                    {  
                        speed = B38400;   
                    }  
                    break;  
                case 230400:  
                    {  
                        speed = B230400;      
                    }  
                    break;  
                case 115200:  
                default:  
                    {  
                        speed = B115200;      
                    }  
                    break;  
                  
              
        }  
          
        /*实际波特率的设置*/  
        cfsetispeed(&new_cfg,speed);  
          
        cfsetospeed(&new_cfg,speed);  
          
          
        /*5.数据位设置*/  
        switch(data_bit)  
        {  
                case 7:  
                    {  
                        new_cfg.c_cflag |= CS7;  
                    }  
                    break;  
                      
                default:  
                case 8:  
                    {  
                        new_cfg.c_cflag |= CS8;  
                    }  
                    break;  
              
        }  
          
          
        /*6.奇偶校验位*/  
        switch(parity)  
        {  
                default:  
                case 'n':  
                case 'N':  
                    {  
                        new_cfg.c_cflag &= ~PARENB;  
                        new_cfg.c_iflag &= ~INPCK;  
                    }  
                    break;  
                case 'o':  
                case 'O':  
                    {  
                          
                        new_cfg.c_cflag |= (PARENB | PARODD);  
                        new_cfg.c_iflag |= INPCK;  
                    }  
                    break;  
                      
                case 'e':  
                case 'E':  
                    {  
                          
                        new_cfg.c_cflag |= PARENB;  
                        new_cfg.c_cflag &= ~PARODD;  
                        new_cfg.c_iflag |= INPCK;  
                    }  
                    break;  
                      
                case 's':  
                case 'S':  
                    {  
                          
                        new_cfg.c_cflag &= ~PARENB;  
                        new_cfg.c_cflag &= ~CSTOPB;  
                    }  
                    break;  
        }  
          
          
        /*7.设置停止位*/  
        switch(stop_bit)  
        {  
                case 1:  
                default:  
                    {  
                        new_cfg.c_cflag &= ~CSTOPB;    
                    }  
                break;  
                  
                case 2:  
                    {  
                        new_cfg.c_cflag |= CSTOPB;  
                    }  
                break;  
              
        }  
          
        /*8.设置最少字符和等待时间*/  
        new_cfg.c_cc[VTIME] = 0;  
        new_cfg.c_cc[VMIN] = 1;  
          
        /*9.清除串口缓冲 
                    TCIFLUSH：对接收到而未被读取的数据进行清空处理 
                    TCOFLUSH：对尚未传送成功的输出数据进行清空处理 
                    TCIOFLUSH：包括前两种功能，即对尚未处理的数据进行处理 
        */  
        tcflush(fd,TCIFLUSH);  
          
          
        /*10.激活配置 
            TCSANOW：配置的修改立即生效 
            TCSADRAIN：配置的修改在所有写入fd的输出都传输完毕之后生效 
            TCSAFLUSH：所有已接收但未读入的输入都将在修改生效之前被丢弃 
        */  
        if((tcsetattr(fd,TCSANOW,&new_cfg)) != 0)  
        {  
                perror("tcsetattr");  
                return -1;        
          
        }  
          
          
        return 0;  
}  
  
  

int open_uart(int com_port)  
{  
        int fd;  
#ifdef COMGERAL  
        char *dev[] = {"/dev/ttyS1","/dev/ttySAC1","/dev/ttySAC2","/dev/ttySAC3"};  
#else  
        char *dev[] = {"/dev/ttyS1","/dev/ttyUSB1","/dev/ttyUSB2","/dev/ttyUSB3"};  
#endif  
  
        if((com_port < 0) || (com_port > MAX_COM_NUM))  
            {  
                return -1;  
                  
            }  
              
        fd = open(dev[com_port -1],O_RDWR | O_NOCTTY | O_NDELAY);  
          
        if(fd < 0)  
            {  
                perror("open");  
                return -1;  
            }  
              
        if(fcntl(fd,F_SETFL,0) < 0)  
            {  
                  
                perror("fcntl");  
                return -1;   
            }  
              
        if(isatty(fd) == 0)  
            {  
                perror("This is not a tty");  
                return -1;  
            }  
              
            return fd;  
}  
  
  
