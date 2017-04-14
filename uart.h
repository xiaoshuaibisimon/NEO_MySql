
#ifndef __UART_H__
#define __UART_H__


#include <stdio.h>  
#include <stdlib.h>  
#include <string.h>  
#include <sys/types.h>  
#include <sys/stat.h>  
#include <errno.h>  
#include <termios.h>  
#include <fcntl.h>  
#include <sys/time.h>  
#include <unistd.h> 


//#define COMGERAL  1  
#define MAX_COM_NUM 4 

#define OPEN_UART_ERR   -1 
#define SET_UART_ERR    -2


typedef unsigned char uint8;

int set_uart_config(int fd,int baudrate,int data_bit,char parity,int stop_bit);
int open_uart(int com_port) ;



#endif

