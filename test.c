
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <mysql.h>
#include "uart.h"
#include "getID.h"
#include "libfahw.h"
#include <locale.h>



/******************************************/
/*宏定义部分*/
/******************************************/
#define CMD_SIZE 4096  
#define BUF_SIZE 1024

#define MAX_SEC 10000


#define PASSWD "caseyviki@811214"
#define PASS_LEN (sizeof(PASSWD))

#define HDR_LENGTH 1
#define READ_HDR_ERR -1
#define READ_DATA_ERR -2
#define READ_FRAME_ERR -3

#define CONNECT_SUCCESS 1
#define CONNECT_FAILED -3


#define CONNECT_BUF_SIZE 128
#define IP_SIZE  32
#define GW_SIZE  32

#define IP_CONFIG_FILE_PATH  ("/etc/network/interfaces")
#define IP_ADDR_INDEX_STRING ("address")
#define GATEWAY_INDEX_STRING ("gateway")

#define NOT_LOGIN 1
#define IS_LOGIN 2

#define NOT_ROOT 3
#define IS_ROOT 4

#ifdef ACK_NUMBER

//login
#define LOGIN_FAILED -1
#define LOGIN_SUCCESS 1

//set IP
#define SET_IP_OK 2
#define SET_IP_BAD -2

#define SET_GW_OK 7
#define SET_GW_BAD -7




//connect
#define CONNECT_BAD -3
#define CONNECT_OK 3

//set name
#define SET_NAME_BAD -4

//read cmd
#define READ_CMD_BAD -5

//excute cmd
#define QUERY_BAD -6

#endif



/******************************************/
/*全局变量*/
/******************************************/
char                  cmd_buf[CMD_SIZE];     
fd_set                inset,tempset;  
unsigned int          data_len;
char                  login_flag; 
char                  reset_flag = 0;
char g_mac[(MAC_SIZE*2)+1]={0};
char g_cpu_id[ID_SIZE]={0};
char g_sd_id[ID_SIZE]={0};
char login_passwd[PASSWD_SIZE]={0};


char * g_key = "AlohaJun";



/******************************************/
//函数功能：在NEO的终端和串口工具上都显示提示信息

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//str--要显示的提示信息

//返回值：无
/******************************************/
void my_printf(int fd,char * str)
{
    printf("%s",str);
    write(fd,str,strlen(str));
}



/******************************************/
//函数功能：从串口读取指定长度的数据

//参数：
//length--准备读取的字节长度
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）

//返回值：
//成功--返回实际读取到的字节数，如果没有读取到任何字节不返回
//失败--返回READ_FRAME_ERR错误码
/******************************************/
int readFrameData(unsigned int length,int fd,struct timeval *tv)
{
  char tmp_buf[BUF_SIZE]={0}; //临时缓冲区--存放每一次读取的数据
  int res = 0;
    
  unsigned int cnt_read = 0;//已经读取到的总字节数
  unsigned int cnt_need;//还需要的字节数（只针对完整读取指定长度的数据包）
  unsigned int cnt_tmp;//存放本次读取到的临时长度
  
  memset(tmp_buf,0,BUF_SIZE);
  memset(cmd_buf,0,CMD_SIZE);
  do{
      //更新文件描述符集合和监视时间参数
      tempset = inset;
      tv->tv_sec = 0;  
      tv->tv_usec = MAX_SEC;
      if(login_flag == NOT_LOGIN || login_flag == NOT_ROOT)  
      {
          tv->tv_sec = 1;  
          tv->tv_usec = 0;
      }
      res = select(fd + 1,&tempset,NULL,NULL,tv);   
      if(res == 0)//超时
      { 
          if(cnt_read > 0)//只读到了一部分数据--不足指定长度
          {
            return cnt_read; 
          }
          if(login_flag == NOT_LOGIN)
          {
            my_printf(fd,"input login passwd\r\n");
          }
          if(login_flag == NOT_ROOT)
          {
            my_printf(fd,"input root passwd\r\n");
          }  
      }
      else if (res < 0) //select出错 
          my_printf(fd,"select error\n");  
      else  //有数据可读取
      {  
          if(FD_ISSET(fd,&inset))
          {
              memset(tmp_buf,0,BUF_SIZE); 
              cnt_need = length - cnt_read;//更新当前还需要读取的字节数
              cnt_tmp = read(fd,tmp_buf,cnt_need);
              if(cnt_tmp > 0)//读取是否成功
              {
                
                memcpy(cmd_buf+cnt_read,tmp_buf,cnt_tmp);//每次都把数据累加到cmd_buf
                cnt_read +=  cnt_tmp;
                if(cnt_read == length)//是否读取了指定长度
                {
                  return cnt_read;
                }
              }
              else if(cnt_tmp == -1)//读串口失败
              {
                  return READ_FRAME_ERR;
              }  
          }   
      }    
      
    }while(1);   
}


/******************************************/
//函数功能：从串口读取完整的一个数据包

//说明：
//串口发过来的数据分为  包头+有效数据  两个部分，
//读取的时候也要分为两次读取，先读取包头获得有效数据的长度，
//再根据该长度读取实际的有效数据，从而完成一个数据包的完整读取
//其中包头部分目前规定只有一个字节，就是该数据包的长度，
//也就是说目前串口发过来的命令一次不能超过255字节

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）

//返回值：
//成功--返回有效数据长度
//失败--返回错误码
/******************************************/
int readPkt(int fd,struct timeval *tv)
{
    int ret = 0;
    
    //读取包头
    ret = readFrameData(HDR_LENGTH,fd,tv);
    if(ret != HDR_LENGTH)
      return READ_HDR_ERR;//读取包头错误
      
    //获取有效长度  
    data_len = (unsigned char)cmd_buf[0];
    
    //读取有效数据
    ret = readFrameData(data_len,fd,tv);
    if(ret != data_len)
      return READ_DATA_ERR;//读取有效数据错误
      
    return ret;//返回有效数据长度
}

/******************************************/
//函数功能：更新网络配置文件

//参数：
//path--配置文件的绝对路径
//mode--打开配置文件的模式
//index_str--用于定位IP/网关地址在配置文件的哪一行，该行开头的字符串就是index_str
//IP_str--从串口获取到的新IP/网关地址

//返回值：
//成功--返回0
//失败--返回错误码
/******************************************/

#define OPEN_FILE_ERR -5
#define READ_FILE_ERR -6
#define FSEEK_ERR -7
int setIPConfig(const char * path,const char * mode,const char *index_str,const char *IP_str)
{
 
    char work[50]="";//缓冲区--从文件读取的数据存放于临时缓冲区
    char buffer1[50]="";//一行中的前半部分（索引），从配置文件读取的每一行，都分为三部分，中间用空格隔开
    char buffer2[50]="";//一行中的中半部分（待修改内容）
    char buffer3[50]="";//一行中的后半部分（保持不动）
    
    
    int len=0;
    int res;
    int src_len = 0;
    int dst_len = 0;
    
    FILE *fp =fopen(path,mode);
    if(fp==NULL)
    {
        printf("open failed\r\n");
        return OPEN_FILE_ERR;
    }

    while(1) 
    {
        if(!fgets(work,50,fp))
        {             
            if(ferror(fp))//读取文件过程中出错
            {
              printf("Errorreading...\n");
              clearerr(fp);
              
              fclose(fp);
              return READ_FILE_ERR;
            }
        }
 
        len=strlen(work);
        //printf("%s\r\n",work);
        sscanf(work,"%s%s",buffer1,buffer2);//分割每一行
        
        if(!strncmp(index_str,work,strlen(index_str)))//找到IP/网关地址所在的行
        {
            res=fseek(fp,-len,1);
            if(res<0)
            {
                perror("fseek");
                return FSEEK_ERR;
            }
            src_len = strlen(buffer2);
            dst_len = strlen(IP_str);
            strcpy(buffer2,IP_str);
            if(src_len > dst_len)
            {
                memset(buffer3,' ',50);
                buffer3[49] = '\0';
                strncat(buffer2,buffer3,(src_len - dst_len));
            }
            
            
            strcat(buffer1," ");
            strcat(buffer1,buffer2);

            fprintf(fp,"%s",buffer1);//写入新的IP/网关地址
            fclose(fp);
            break;
        }
        if(feof(fp))//如果读到文件末尾了就break--不必再进行下一次循环读取文件
        {
            printf("We have reached the end of file\n");
            fclose(fp);
            break;
        }
    }
    
 
    return 0;
 
}

/******************************************/
//函数功能：从串口获取IP地址并设置到NEO

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）


//返回值：
//成功--返回0
//失败--返回错误码
/******************************************/

#define SET_IP_ERR -8
int setNEO_IP(int fd,struct timeval *tv)
{
    char IP[IP_SIZE]={0};
    char set_ip_cmd[128] = {0};//最后执行立即更新IP地址的命令字符串
    
    int ret = 0;
    
    my_printf(fd,"input IP address\r\n");
    ret = readPkt(fd,tv);//获取串口输入的IP地址
    if(ret < 0)
    {
      if(ret == READ_HDR_ERR)
          my_printf(fd,"read IP head err\r\n");
      else  if(ret == READ_DATA_ERR)
          my_printf(fd,"read IP data err\r\n");
          
      return SET_IP_ERR;//读取串口数据的时候出错
    }    

    if ( 0 == strncmp(cmd_buf , "reset" , 5) || 0 == strncmp(cmd_buf , "RESET" , 5) ) 
    {
        my_printf(fd,"reset the app\r\n");
        reset_flag = 1;//重启标志置位--需要重启
        return 0;
    }  
    memcpy(IP,cmd_buf,data_len);
    
    ret = setIPConfig(IP_CONFIG_FILE_PATH,"r+",IP_ADDR_INDEX_STRING,(const char *)IP);//更新IP地址
    if(ret != 0)
    {
        return SET_IP_ERR;
    }
    
    //立即更新IP地址
    strcpy(set_ip_cmd,"ifconfig eth0 ");
    strcat(set_ip_cmd,IP);
    system(set_ip_cmd);

    return 0;
}


/******************************************/
//函数功能：从串口获取网关地址并设置到NEO

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）


//返回值：
//成功--返回0
//失败--返回错误码
/******************************************/

#define SET_GW_ERR -18
int setNEO_GW(int fd,struct timeval *tv)
{
    char GW[GW_SIZE]={0};
    char set_gw_cmd[128] = {0};//最后执行立即更新网关地址的命令字符串
    
    int ret = 0;
    
    my_printf(fd,"input GateWay\r\n");
    ret = readPkt(fd,tv);//获取串口输入的网关地址
    if(ret < 0)
    {
      if(ret == READ_HDR_ERR)
          my_printf(fd,"read GateWay head err\r\n");
      else  if(ret == READ_DATA_ERR)
          my_printf(fd,"read GateWay data err\r\n");
          
      return SET_GW_ERR;//读取串口数据的时候出错
    }   

    //如果是reset就返回连接成功--以便外层调用跳出循环--实现重启APP
    if ( 0 == strncmp(cmd_buf , "reset" , 5) || 0 == strncmp(cmd_buf , "RESET" , 5) ) 
    {
        my_printf(fd,"reset the app\r\n");
        reset_flag = 1;//重启标志置位--需要重启
        return 0;
    }  
    memcpy(GW,cmd_buf,data_len);
    
    ret = setIPConfig(IP_CONFIG_FILE_PATH,"r+",GATEWAY_INDEX_STRING,(const char *)GW);//更新网关地址
    if(ret != 0)
    {
        return SET_GW_ERR;
    }
    
    //立即更新网关地址
    strcpy(set_gw_cmd,"route add default gw ");
    strcat(set_gw_cmd,GW);
    system(set_gw_cmd);
    
    return 0;
}
 

/******************************************/
//函数功能：连接数据库

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）
//connect--连接到数据库服务器的数据库通讯链接（类比TCP里面的链接--通讯套接字）
//mysql--数据库服务器的一个监听链接（类比TCP服务器里面--监听套接字）

//返回值：
//成功--返回CONNECT_SUCCESS
//失败--返回CONNECT_FAILED错误码
/******************************************/

int connect_MySQL(int fd,struct timeval *tv,MYSQL **connect,MYSQL *mysql)
{
    char hostName[CONNECT_BUF_SIZE]={0};//主机名--服务器IP
    char userName[CONNECT_BUF_SIZE]={0};//数据库用户名
    char passWord[CONNECT_BUF_SIZE]={0};//数据库密码
    char DBName[CONNECT_BUF_SIZE]={0};//数据库名
#ifdef ACK_NUMBER
    char ack[1] = {0};
#endif
    int ret = 0;
    
    //1. 获取主机名--服务器IP
    my_printf(fd,"input hostname\r\n");
    ret = readPkt(fd,tv);
    if(ret < 0)
    {
      if(ret == READ_HDR_ERR)
          my_printf(fd,"read hostname head err\r\n");
      else  if(ret == READ_DATA_ERR)
          my_printf(fd,"read hostname data err\r\n");
          
      return CONNECT_FAILED;//读取串口数据的时候出错
    }
    //如果是reset就返回连接成功--以便外层调用跳出循环--实现重启APP
    if ( 0 == strncmp(cmd_buf , "reset" , 5) || 0 == strncmp(cmd_buf , "RESET" , 5) ) 
    {
        my_printf(fd,"reset the app\r\n");
        reset_flag = 1;//重启标志置位--需要重启
        return CONNECT_SUCCESS;
    } 
    memcpy(hostName,cmd_buf,data_len);
    
    
    
    //2. 获取数据库的用户名
    my_printf(fd,"input username\r\n");
    ret = readPkt(fd,tv);
    if(ret < 0)
    {
      if(ret == READ_HDR_ERR)
          my_printf(fd,"read username head err\r\n");
      else  if(ret == READ_DATA_ERR)
          my_printf(fd,"read username data err\r\n");
          
      return CONNECT_FAILED;//读取串口数据的时候出错
    }
    if ( 0 == strncmp(cmd_buf , "reset" , 5) || 0 == strncmp(cmd_buf , "RESET" , 5) ) 
    {
        my_printf(fd,"reset the app\r\n");
        reset_flag = 1;
        return CONNECT_SUCCESS;
    } 
    memcpy(userName,cmd_buf,data_len);
    
    
    
    //3. 获取数据库密码
    my_printf(fd,"input password\r\n");
    ret = readPkt(fd,tv);
    if(ret < 0)
    {
      if(ret == READ_HDR_ERR)
          my_printf(fd,"read password head err\r\n");
      else  if(ret == READ_DATA_ERR)
          my_printf(fd,"read password data err\r\n");
          
      return CONNECT_FAILED;//读取串口数据的时候出错
    }
    if ( 0 == strncmp(cmd_buf , "reset" , 5) || 0 == strncmp(cmd_buf , "RESET" , 5) )
    {
        my_printf(fd,"reset the app\r\n");
        reset_flag = 1;
        return CONNECT_SUCCESS;
    } 
    memcpy(passWord,cmd_buf,data_len);
    
    
    
    //4. 获取数据库名
    my_printf(fd,"input dbname\r\n");
    ret = readPkt(fd,tv);
    if(ret < 0)
    {
      if(ret == READ_HDR_ERR)
          my_printf(fd,"read dbname head err\r\n");
      else  if(ret == READ_DATA_ERR)
          my_printf(fd,"read dbname data err\r\n");
          
      return CONNECT_FAILED;//读取串口数据的时候出错--连接失败
    }
    if ( 0 == strncmp(cmd_buf , "reset" , 5) || 0 == strncmp(cmd_buf , "RESET" , 5) )
    {
        my_printf(fd,"reset the app\r\n");
        reset_flag = 1;
        return CONNECT_SUCCESS;
    } 
    memcpy(DBName,cmd_buf,data_len);
    
    
    //5. 连接数据库服务器
    (*connect) = mysql_real_connect(mysql, hostName, userName, passWord, DBName,0, 0, 0);
    if (*connect == NULL)//连接失败
    {
        printf("connect error, %s\n", mysql_error(mysql));
        my_printf(fd,"connect BAD\r\n");
#ifdef ACK_NUMBER       
        ack[0] = CONNECT_BAD;
        write(fd,ack,1);
#endif
        return CONNECT_FAILED;
    }
    
    my_printf(fd,"connect OK\r\n");
    reset_flag = 0;//连接成功--接下来并不需要重启
#ifdef ACK_NUMBER    
    ack[0] = CONNECT_OK;
    write(fd,ack,1);
#endif
    return CONNECT_SUCCESS;
}






//////////////////////////////////////////////////0307更新////////////////////////////////////////////////////////////////////////////////////////


/******************************************/
//函数功能：超级用户验证--只有厂商才会调用

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）


//返回值：无

//说明：如果验证失败会一直提示输入root密码！
//而且该root密码从串口传过来的时候没有包头，所以不需要分两部分读取
//这样做不让登录密码和数据包协议统一，稍微能增加产品安全性
/******************************************/

void rootConfirm(int fd,struct timeval *tv)
{
  int ret = 0;
#ifdef ACK_NUMBER
  char ack[1]= {0};
#endif
  do{
      //ret = readFrameData(PASS_LEN,fd,tv);//获取root密码
      //if(ret == PASS_LEN)
      //{
        //cmd_buf[ret] = '\0';  
        ////printf("%d  from uart:%s\r\n",ret,cmd_buf);
      //} 
      
      ret = readPkt(fd,tv);
      if(ret < 0)
      {
          my_printf(fd,"read root passwd BAD\r\n");
          continue;
      }
      if(strcmp(cmd_buf,PASSWD) != 0)//判断密码
      {
        my_printf(fd,"root BAD\r\n");
#ifdef ACK_NUMBER
        ack[0] = LOGIN_FAILED;
        write(fd,ack,1);
#endif
        continue;
      }else{
        my_printf(fd,"root OK\r\n");
#ifdef ACK_NUMBER
        ack[0] = LOGIN_SUCCESS;
        write(fd,ack,1);
#endif
        login_flag = IS_ROOT;//修改串口输出标志
        break;
      }
    }while(1);
}






/******************************************/
//函数功能：将16进制数据编码成为文本

//参数：
//data--数据
//n--数据长度
//hex--用于存放输出的字符串

//返回值：输出的字符串的长度。如果输入n为6，则返回值为12
/******************************************/

int Encode(const unsigned char* data, int n, char* hex)
{
	int count = 0;
	int i=0;
	for(; i<n; i++)
	{
		// 转成16进制
		char buf[3];
		sprintf(buf, "%02X", data[i]);

		// 复制到输出 (注意：此处于内嵌的++，而++就是为了方便这种用途的）
		hex[ count++ ] = buf[0];
		hex[ count++ ] = buf[1];
	}
	hex[count] = 0; // 结束符
	return count;
}


/******************************************/
//函数功能：获取网卡的物理地址

//参数：
//net_name--网卡名称
//mac_p--存储已经转换为文本的物理地址


//返回值：成功返回0，失败返回-1
/******************************************/
int getMAC(const char *net_name,char * mac_p)
{
    int sock;
    struct sockaddr_in sin;
    struct sockaddr sa;
    struct ifreq ifr;
    unsigned char  mac[MAC_SIZE]={0};
    
    //1. 创建一个UDP套接字（TCP也可以）
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        perror("socket");
        return -1;		
    }
    
    //2. 填充网卡名称信息
    strncpy(ifr.ifr_name, net_name, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = 0;
    
    
    //3. 清空临时存储区域（16进制的实际物理地址需要的内存--6个字节）
    memset(mac, 0, sizeof(mac));
    
    //4. 通过ioctl获取物理地址
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0)
    {
        perror("ioctl");
        return -1;
    }
    
    //5. 将16进制物理地址复制到临时缓冲区
    memcpy(&sa, &ifr.ifr_addr, sizeof(sin));
    memcpy(mac, sa.sa_data, sizeof(mac));
    //memcpy(mac_p,mac,sizeof(mac));
    
    //6. 将临时缓冲区编码为文本格式并存储到mac_p指向的内存空间
    Encode(mac,sizeof(mac),mac_p);
    //fprintf(stdout, "%s mac: %.2X:%.2X:%.2X:%.2X:%.2X:%.2X\n", ETH_NAME, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    
    return 0;
}


/******************************************/
//函数功能：获取CPU的ID（简单理解为序列号）

//参数：
//cpu_id--指向存储CPU ID的内存空间
//index_str--用来寻找文本文件里面表示CPU ID开头的行的索引字符串


//返回值：成功返回0，失败返回负数
/******************************************/
int getCPU_ID(char * cpu_id,const char * index_str)
{
    FILE *fp = NULL;
    char  tmp[TMP_SIZE] =""; 
    char  buffer1[TMP_SIZE] =""; 
    char  buffer2[TMP_SIZE] =""; 
    char  buffer3[TMP_SIZE] =""; 
    
    
    //1. 打开存储CPU ID的文件
    fp = fopen(CPU_ID_PATH,"r");
    if(fp==NULL)
    {
        printf("open CPU_ID_PATH failed\r\n");
        return -1;
    }
    
    //2. 清空临时缓冲区
    memset(buffer1,0,TMP_SIZE);
    memset(buffer2,0,TMP_SIZE);
    memset(buffer3,0,TMP_SIZE);
    
    //3. 读取文件内容寻找CPU ID
    while(1) 
    {
        //3.1 清空临时缓冲区
        memset(tmp,0,TMP_SIZE);
        
        //3.2 读取一行到临时缓冲区
        if(!fgets(tmp,TMP_SIZE,fp))
        {             
            if(ferror(fp))//读取文件过程中出错
            {
              printf("Errorreading...\n");
              clearerr(fp);
              
              fclose(fp);
              return -2;
            }
        }
 
        //3.3 判断这一行是否是包含CPU ID的行
        if(!strncmp(index_str,tmp,strlen(index_str)))//找到CPU ID所在的行
        {
            //3.4 获取CPU ID
            sscanf(tmp,"%s%s%s",buffer1,buffer2,buffer3);//分割改行为三部分--第三部分是CPU ID
            //printf("%s\r\n",buffer3);
            
            //3.5 复制到cpu_id指向的内存空间
            memcpy(cpu_id,buffer3,strlen(buffer3));
            break;
        }
        
        if(feof(fp))//如果读到文件末尾了就break--不必再进行下一次循环读取文件
        {
            printf("We have reached the end of file\n");
            fclose(fp);
            break;
        }
    }
    
    return 0;
    
}


/******************************************/
//函数功能：获取SD卡的ID（简单理解为序列号）

//参数：
//sd_id--指向存储SD卡 ID的内存空间

//返回值：成功返回0，失败返回负数
/******************************************/
int getSD_ID(char * sd_id)
{
    FILE *fp = NULL;
   
    char  tmp[TMP_SIZE] ="";  
    char  real_path[PATH_SIZE]="";
    int i = 0;
    
    //1. 打开存储cid文件的路径的文件
    fp = fopen(SD_ID_PATH,"r");
    if(fp==NULL)
    {
        printf("open SD_PATH failed\r\n");
        return -1;
    }
    //2. 读取cid所在的绝对路径
    memset(real_path,0,PATH_SIZE);
    while(1) 
    {
        //2.1 每次都将读取到的内容写入real_path缓冲区
        if(!fgets(real_path+i,PATH_SIZE,fp))
        {             
            if(ferror(fp))//读取文件过程中出错
            {
              printf("Errorreading...\n");
              clearerr(fp);
              
              fclose(fp);
              return -2;
            }
        }
        
        //2.2 修正第二次开始写入的偏移量
        i =  strlen(real_path);
        if(feof(fp))//如果读到文件末尾了就break--不必再进行下一次循环读取文件
        {
            fclose(fp);//记得关闭文件
            break;
        }
    }
    
    //3. 由于存储cid路径的文件最后包含了换行符'\n'，所以需要将其替换为字符串结束符--否则找不到指定的文件
    fp = NULL;
    //printf("%s",real_path);
    real_path[strlen(real_path)-1] = '\0';
    
    //4. 打开存储SD卡ID的cid文件
    fp = fopen((const char *)real_path,"r");
    if(fp==NULL)
    {
        perror("fopen");
        printf("open SD_ID_PATH failed\r\n");
        return -1;
    }
    
    //5. 获取SD 卡的ID
    i = 0;
    memset(tmp,0,TMP_SIZE);    
    while(1) 
    {
       
        if(!fgets(tmp+i,TMP_SIZE,fp))//每次都写入临时缓冲区tmp
        {             
            if(ferror(fp))//读取文件过程中出错
            {
              printf("Errorreading...\n");
              clearerr(fp);
              
              fclose(fp);
              return -2;
            }
        }
        i =  strlen(tmp);//修正第二次写入的位置
        if(feof(fp))//如果读到文件末尾了就break--不必再进行下一次循环读取文件
        {
            fclose(fp);
            
            //6. 读取完文件内容将其复制到sd_id指向的内存空间
            memcpy(sd_id,tmp,strlen(tmp));
            break;
        }
    }
    
    return 0;
}


/******************************************/
//函数功能：获取系统的三个ID

//参数：无

//返回值：无
/******************************************/
void getAllID()
{
    //1. 清空三个全局变量数组--用于存储系统三个ID
    memset(g_cpu_id,0,sizeof(g_cpu_id));
    memset(g_mac,0,sizeof(g_mac));
    memset(g_sd_id,0,sizeof(g_sd_id));
    
    //2. 获取CPU ID
    getCPU_ID(g_cpu_id,CPU_INDEX);
    
    //3. 获取eth0的MAC地址
    getMAC(ETH_NAME,g_mac);
    
    //4. 获取SD卡的ID
    getSD_ID(g_sd_id);
}


/******************************************/
//函数功能：加密指定的数组

//参数：
//p--指向明文
//size--明文长度（以字节为单位）
//key--秘钥（在全局变量里固定为8个字节的数组）
//c--指向存储密文的内存空间

//返回值：无
/******************************************/
void myEncrypt(const unsigned char* p, int size, const char* key,unsigned char* c)
{
  int i=0;
	for(; i<size; i++)
	{
		c[i] = p[i] + key[i%8]; // 
	}
}


/******************************************/
//函数功能：解密指定的数组

//参数：
//c--指向密文
//size--密文长度（以字节为单位）
//key--秘钥（在全局变量里固定为8个字节的数组）
//p--指向存储明文的内存空间

//返回值：无
/******************************************/
void myDecrypt(const unsigned char* c, int size, const char* key,unsigned char* p)
{
  int i=0;
	for(; i<size; i++)
	{
		p[i]= c[i] - key[i%8]; // 
	}
}



/******************************************/
//函数功能：更新登录密码

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）

//返回值：成功返回0，失败返回负数
/******************************************/
int set_passwd(int fd,struct timeval *tv)
{
    unsigned char  realPlain[DES_SIZE] =""; 
    unsigned char  cipher[DES_SIZE] ="";
    int ret = 0;
    int pswd_fd = 0;
    char buf1[PASSWD_SIZE] = {0};
    char buf2[PASSWD_SIZE] = {0};
    
    //0. 验证超级用户密码
    
    login_flag = NOT_ROOT;
    rootConfirm(fd,tv);
    
    //1. 打开存储密码密文的文件
    pswd_fd = open(PASSWD_FILE,O_RDWR|O_TRUNC);
    if(pswd_fd == -1)
    {
        perror("open fd");
        return -2;
    }
    
    //2. 获取用户输入的新密码
    do
    {
      //2.1 第一次新密码
      my_printf(fd,"input new login passwd\r\n");
      ret = readPkt(fd,tv);
      if(ret < 0)
      {
          my_printf(fd,"read new passwd BAD\r\n");
          continue;
      }
      memset(buf1,0,sizeof(buf1));
      strcpy(buf1,cmd_buf);
      
      //2.2 第二次新密码
      my_printf(fd,"repeat new login passwd\r\n");
      ret = readPkt(fd,tv);
      if(ret < 0)
      {
          my_printf(fd,"read new passwd BAD\r\n");
          continue;
      }
      memset(buf2,0,sizeof(buf2));
      strcpy(buf2,cmd_buf);
      
      //2.3 判断两次设置的新密码是否一致
      if(strcmp(buf1,buf2) != 0)
      {
          my_printf(fd,"new login passwd not same\r\n");
          continue;//不一致--重新设置新密码
      }
      break;//一致--不再获取串口输入的新密码
      
    }while(1);      
                
    //3. 获取需要的三个ID（CPU、 MAC、 SD）
    getAllID();
    
    //4. 将四个原文组合
    memset(realPlain,0,sizeof(realPlain));
    memcpy(realPlain,g_cpu_id,strlen(g_cpu_id));
    memcpy(realPlain+strlen(g_cpu_id),g_mac,strlen(g_mac));
    memcpy(realPlain+strlen(g_cpu_id)+strlen(g_mac),g_sd_id,strlen(g_sd_id));
    memcpy(realPlain+strlen(g_cpu_id)+strlen(g_mac)+strlen(g_sd_id),cmd_buf,strlen(cmd_buf));
    
    //5. 加密得到密文
    memset(cipher,0,sizeof(cipher));
    myEncrypt(realPlain, strlen((char *)realPlain), g_key,cipher);
    
    //6. 将密文写入文件保存
    write(pswd_fd,cipher,strlen((char *)realPlain));
    
    //7. 更新串口输出标志--决定了是输出哪一个字符串提示用户输入
    login_flag = IS_LOGIN;
    my_printf(fd,"set login passwd OK\r\n");
    
    return 0;
}


/******************************************/
//函数功能：登录验证

//参数：
//fd--从哪个设备文件读取（留待以后扩展多串口同时使用）
//tv--IO多路复用用到的监视时间参数（不能让串口阻塞式的读取--为以后支持多任务）


//返回值：成功返回0，失败返回负数
/******************************************/
int loginConfirm(int fd,struct timeval *tv)
{
      int passwd_fd = 0;
      unsigned char  plain[DES_SIZE] ="";
      unsigned char  cipher[DES_SIZE] ="";
      unsigned char  realPlain[DES_SIZE] =""; 
      int read_cnt = 0;
      int ret = 0;
      int i = 0;
      char is_open =  0;
      struct stat buf_stat;//stat结构体存放文件状态信息
      
      memset(plain,0,sizeof(plain));
      memset(cipher,0,sizeof(cipher));
      
      //0. 判断密码文件是否存在
      passwd_fd = open(PASSWD_FILE,O_RDWR | O_CREAT | O_EXCL,0755);
      if(passwd_fd == -1)
      {
          if(errno == EEXIST)/*如果目标文件同名文件已存在*/
          {
              //1.0 若文件存在则重新打开文件
              
              ret = stat(PASSWD_FILE,&buf_stat);//穿透软链接直接查看原始文件的状态信息
              if(ret == -1)
              {
                perror("stat");
                return -1;
              }
              do
              {
                  //printf("file size = : %d\n",(int)buf_stat.st_size);
                  //1.1 判断文件内容是否为空
                  if((int)buf_stat.st_size <= 0)//为空--直接重新设置登录密码（表示还未设置登录密码）
                  {
                      is_open = 1;//此时文件并未打开，所以跳出去重新设置密码的时候不能再关闭
                      break;
                  }
                  
                  //1.2 不为空--密码文件真实有效，已经设置过登录密码--重新打开文件
                  passwd_fd = open(PASSWD_FILE,O_RDONLY);//只读--进行密码匹配，不需修改
                  if(passwd_fd == -1)
                  {
                      perror("open fd");
                      return -2;
                  }
                  
                  //1.3. 读取文件到密文缓冲区
                  while(read_cnt = read(passwd_fd,cipher,DES_SIZE))
                  {
                      //1.4. 解密得到原文
                      myDecrypt(cipher,read_cnt,g_key,plain+i);
                      i += read_cnt;//修正下一次明文写入的位置
                  }
                  
                  //1.5 文件读取完毕，关闭文件
                  close(passwd_fd);
                  //my_printf(fd,(char *)plain);
                 
                 //1.6 进行登录密码验证
                  do
                  {
                    //1.6.1 从串口获取用户输入的登录密码
                    ret = readPkt(fd,tv);
                    if(ret < 0)
                    {
                        my_printf(fd,"read login passwd BAD\r\n");
                        continue;
                    }
                    
                    //1.6.2 如果直接输入重置登录密码的命令--重置密码，置位重启标志
                    if (0 == strncmp(cmd_buf , "set login passwd" , 16) || 0 == strncmp(cmd_buf , "SET LOGIN PASSWD" , 16) ) //如果是quit则结束程序
                    {
                        set_passwd(fd,tv);//重置密码
                        //login_flag = NOT_LOGIN;
                        reset_flag = 1;//重启标志置位--上层调用返回后根据该标志决定是否重启
                        break;
                    }   
                          
                    
                    //1.6.3 获取三个ID
                    getAllID();
                    
                    //1.6.4 组合现在输入的登录密码和当前系统的三个ID并与解密得到的原文比较
                    memset(realPlain,0,sizeof(realPlain));
                    memcpy(realPlain,g_cpu_id,strlen(g_cpu_id));
                    memcpy(realPlain+strlen(g_cpu_id),g_mac,strlen(g_mac));
                    memcpy(realPlain+strlen(g_cpu_id)+strlen(g_mac),g_sd_id,strlen(g_sd_id));
                    memcpy(realPlain+strlen(g_cpu_id)+strlen(g_mac)+strlen(g_sd_id),cmd_buf,strlen(cmd_buf));
                    //my_printf(fd,(char *)realPlain);
                    
                    if(strcmp((char *)realPlain,(char *)plain) != 0)//匹配失败--再次获取登录密码进行验证
                    {
                        my_printf(fd,"login BAD\r\n");
                        continue;
                    }
                    else//匹配成功--跳出循环，返回上层调用
                    {
                        my_printf(fd,"login OK\r\n");
                        login_flag = IS_LOGIN;
                        break;
                    }
                    
                    
                  }while(1);
                  
                  return 0;
              }while(0);
          
          }else{/*打开文件发生其他错误*/
              close(passwd_fd);
              perror("open fd");
              return -1;
          }
      }
      
      //2.1. 若文件不存在则新建文件成功（能够执行到这里表示新建文件成功）--第一次启动--设置新密码 
      if(is_open == 0)
        close(passwd_fd);
      //2.2 设置新的登录密码--设置完以后可以继续设置网关等其他参数继续使用，程序不会重启（仅针对第一次启动）
      set_passwd(fd,tv);
       
      return 0; 
}

/******************************************/
//函数功能：初始化GPIO

//参数：
//pin--GPIO对应的序号

//返回值：成功返回0，失败返回负数
/******************************************/
int gpio_init(int pin)
{
    int  ret,board;
    
    //1. 板级初始化
    if ((board = boardInit()) < 0){
        printf("Fail to init board\n");
        return -1;
    }
    
    //2. 导出GPIO可在用户空间使用 
    if ((ret = exportGPIOPin(pin)) == -1) {
        printf("exportGPIOPin(%d) failed\n", pin);
        return -2;
    }
    
    //3. 设置GPIO的工作模式--输出模式
    if ((ret = setGPIODirection(pin, GPIO_OUT)) == -1) {
        printf("setGPIODirection(%d) failed\n", pin);
        return -3;
    }
    
    //4. 默认输出高电平--不点亮LED
    if ((ret = setGPIOValue(pin, GPIO_HIGH)) > 0) {
        printf("GPIO_PIN(%d) value is %d\n", pin, GPIO_HIGH);
    } else {
        printf("setGPIOValue(%d) failed\n", pin);
    }
        
    return 0;
}

//主函数 
int main(void)  
{  
   int                   fd = 0; 
   int                   quit_flag = 0;
   struct timeval        tv;
#ifdef ACK_NUMBER
   char                  ack[1] = {0};
#endif
   
   
   int             ret = 0, i=0;
   MYSQL           mysql;
   MYSQL           *connect;
   MYSQL_RES       *result;
   MYSQL_ROW       row;
   MYSQL_FIELD     *fields;
   unsigned int    num_fields;
   int led_blue = GPIO_PIN(7);
   int led_green = GPIO_PIN(12);
   //setlocale(LC_ALL,"zh_CN.UTF-8");
   
   //重启程序就是从这里开始重启 
   while(1)
   {
       data_len = 0;
       login_flag = NOT_LOGIN;
       reset_flag = 0;
       
       system("find_sd.sh");
       memset(cmd_buf,0,CMD_SIZE); 
     
        //0. 打开串口        
        if((fd = open_uart(1)) < 0)  
        {  
            perror("open port");  
            return -1;  
        }  
              
        if(set_uart_config(fd,115200,8,'N',1) < 0)  
        {  
            perror("set com_config");  
            return -1;  
        }  
      
        //1. 设置监听描述符集合
        FD_ZERO(&inset);
        FD_SET(fd,&inset);  
              
        tv.tv_sec = 1;  
        tv.tv_usec = 0;
        
        
        gpio_init(led_blue);
        gpio_init(led_green);

	//my_printf(fd,"测试中文\r\n");
        
        //2. 等待验证--验证通过启动配置过程
    
        loginConfirm(fd,&tv);
        //2.1 如果在验证密码的过程中重置了登录密码--对应于登录密码忘记（之前已经设置过了）或者是SD卡CPU或者网卡三者并不是完全吻合
        if(reset_flag == 1)
        {
            reset_flag = 0;
            continue;
        }

        //2.2 如果验证通过，只将PIN7对应的LED点亮    
        setGPIOValue(led_blue, GPIO_LOW);
        setGPIOValue(led_green, GPIO_HIGH);
		    
        //3. 更新串口等待时间
        tv.tv_sec = 0;  
        tv.tv_usec = MAX_SEC;
    

        //4. 初始化连接处理程序
        mysql_init(&mysql);
		    
        
        //5. 设置网关--直到设置正确位置    
         do
        { 
          ret = setNEO_GW(fd,&tv);
          if(ret != 0)
          {
              my_printf(fd,"set GateWay BAD\r\n");
#ifdef ACK_NUMBER
              ack[0] = SET_GW_BAD;
#endif
          }
          else if(ret == 0)
          {
	      if(reset_flag != 1)
	      {
		   my_printf(fd,"set GateWay OK\r\n");
	      }
              break;
#ifdef ACK_NUMBER
              ack[0] = SET_GW_OK;
#endif
          }
#ifdef ACK_NUMBER
          write(fd,ack,1);
#endif
        }while(1);
	if(reset_flag == 1)
        {
            reset_flag = 0;
            continue;
        }
    
    
        //6. 设置IP--直到设置正确位置    
        do
        {
          ret = setNEO_IP(fd,&tv);
          if(ret != 0)
          {
              my_printf(fd,"set IP BAD\r\n");
#ifdef ACK_NUMBER
              ack[0] = SET_IP_BAD;
#endif
          }
          else if(ret == 0)
          {
              if(reset_flag != 1)
	      {
		   my_printf(fd,"set IP OK\r\n");
	      }
              break;
#ifdef ACK_NUMBER
              ack[0] = SET_IP_OK;
#endif
          }
#ifdef ACK_NUMBER
          write(fd,ack,1);
#endif
        }while(1);
	if(reset_flag == 1)
        {
            reset_flag = 0;
            continue;
        }
    
    
        //6. 等待串口发送配置参数连接数据库服务器--在此过程中可输入重启命令进行重启
        
        while((ret = connect_MySQL(fd,&tv,&connect,&mysql)) == CONNECT_FAILED);
        
        //6.1 在连接数据库服务器的过程中输入重启命令--重启程序
        if(reset_flag == 1)
        {
            reset_flag = 0;
            continue;
        }
        
        //6.2 连接成功--表示配置正确、数据库可用，将PIN7以及PIN12对应的LED都点亮
        setGPIOValue(led_blue, GPIO_HIGH);
        setGPIOValue(led_green, GPIO_LOW);
        
        //7. 设置字符集
        ret = mysql_query(connect, "SET NAMES utf8");       //设置字符集为UTF8
        if (ret != 0)
        {
            printf("SET NAMES ERR, %s\n", mysql_error(&mysql));
            my_printf(fd,"set names BAD\r\nAPP on NEO is dead\r\n");
#ifdef ACK_NUMBER
            ack[0] = SET_NAME_BAD;
            write(fd,ack,1);
#endif
            return ret;
        }
        my_printf(fd,"set names OK\r\n");
        
        //8. 死循环--接收串口发送的数据并处理
        
        while(1)
        {
            //8.0 等待串口数据发送完毕
            ret = readPkt(fd,&tv);
            if(ret < 0)
            {
                my_printf(fd,"read cmd BAD\r\n");
		setGPIOValue(led_blue, GPIO_HIGH);
        	setGPIOValue(led_green, GPIO_HIGH);
#ifdef ACK_NUMBER
                ack[0] = READ_CMD_BAD;
                write(fd,ack,1);
#endif
                continue;
            }      
            //8.1判断是否是exit或者quit是则退出循环--扫尾工作并结束本次对数据库的访问，等待下一次数据库操作（暂时屏蔽了该功能）
            if ( 0 == strncmp(cmd_buf , "exit" , 4) || 0 == strncmp(cmd_buf , "EXIT" , 4)
            	|| 0 == strncmp(cmd_buf , "quit" , 4) || 0 == strncmp(cmd_buf , "QUIT" , 4) ) 
            {
                //quit_flag = 1;
                //my_printf(fd,"APP on NEO is dead\r\n");
                //break;
                continue;//目前直接进入下一次串口数据的获取
               
            }
            
            //8.2 判断是否是reset重启命令
            if (0 == strncmp(cmd_buf , "reset" , 5) || 0 == strncmp(cmd_buf , "RESET" , 5) )
            {
                my_printf(fd,"reset this app\r\n");
                break;//跳出本循环到外层mysql_close函数处
            } 
            
            //8.3 判断是否是重置登录密码命令
            if (0 == strncmp(cmd_buf , "set login passwd" , 16) || 0 == strncmp(cmd_buf , "SET LOGIN PASSWD" , 16) ) //如果是quit则结束程序
            {
                if(set_passwd(fd,&tv) == 0)//设置完新的登录密码以后继续接收串口数据并处理
		{	
			setGPIOValue(led_blue, GPIO_LOW);
        		setGPIOValue(led_green, GPIO_LOW);
		}
		else 
		{
			setGPIOValue(led_blue, GPIO_HIGH);
        		setGPIOValue(led_green, GPIO_HIGH);
		}


                continue;
            }      
            
            //8.4 执行命令
            ret = mysql_query(connect, cmd_buf); 
            if (ret != 0 )
            {
               printf("func mysql_query() err: %s", mysql_error(&mysql) );
               my_printf(fd,"query SQL BAD\r\n");
	       setGPIOValue(led_blue, GPIO_HIGH);
               setGPIOValue(led_green, GPIO_HIGH);
#ifdef ACK_NUMBER
               ack[0] = QUERY_BAD;
               write(fd,ack,1);
#endif
               continue;
            }
            else
            {
                 my_printf(fd,"query SQL OK\r\n");//执行SQL语句成功--向串口输出query SQL OK
		 setGPIOValue(led_blue, GPIO_LOW);
        	 setGPIOValue(led_green, GPIO_LOW);
            }
            
            //8.4 向串口返回查询结果--等待写完毕
            if ((strncmp(cmd_buf, "select", 6) == 0) || (strncmp(cmd_buf, "SELECT", 6) == 0) ||
              (strncmp(cmd_buf, "show", 4) == 0) || (strncmp(cmd_buf, "SHOW", 4) == 0) ||
              (strncmp(cmd_buf, "desc", 4) == 0) || (strncmp(cmd_buf, "DESC", 4) == 0) )
             {
                    //8.4.1 获取查询结果
                    result = mysql_store_result(&mysql); 
    
                    //8.4.2 获取列表头信息信息
                    fields = mysql_fetch_fields(result);
                    num_fields = mysql_num_fields(result);
    
                    for (i=0; i<num_fields; i++)
                    {
                       printf("%s\t", fields[i].name);
                       memset(cmd_buf,0,CMD_SIZE);
                       ret = sprintf(cmd_buf,"%s\t", fields[i].name);
                       write(fd,cmd_buf,ret);
                    }
                    printf("\n");
                    write(fd,"\r\n",2);
    
                    //8.4.3 按照行获取数据 检索结果集的下一行。
                    while(row = mysql_fetch_row(result))
                    {
                        for (i=0; i<mysql_num_fields(result); i++)
                        {
                            printf("%s\t ",row[i]);
                            memset(cmd_buf,0,CMD_SIZE);
                            ret = sprintf(cmd_buf,"%s\t ",row[i]);
                            write(fd,cmd_buf,ret);
                        }
                        printf("\n");
                        write(fd,"\r\n",2);
			usleep(1000*10);
                    }
    
                    //8.4.4  释放查询结果缓冲区
                    mysql_free_result(result);     //free result after you get the result
             }           
        }
        
        //9. 断开与SQL server的连接（执行到这里表示要重启程序了）
        mysql_close(connect);
        
        //10. 关闭串口
        close(fd); 
        
        //11. 不导出GPIO
        unexportGPIOPin(led_blue);
        unexportGPIOPin(led_green);
        
        //12. 如果结束标志置位--结束整个程序，而不只是重启程序--需要断电重启（暂时屏蔽了该功能--即不会结束程序）
        if(quit_flag == 1)
        {
            break;
        }    
    }
     
    return 0;     
} 




