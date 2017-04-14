#ifndef  __GET_ID_H__
#define  __GET_ID_H__

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>
#include <errno.h>
#include<stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#define ETH_NAME	"eth0"
#define TMP_SIZE  256
#define MAC_SIZE  6
#define ID_SIZE    64
#define PASSWD_SIZE  128

#define CPU_ID_PATH  "/proc/cpuinfo"

#define SD_ID_PATH  "/home/fa/.sd_path"

#define PASSWD_FILE "/etc/.passwd_file"

#define CPU_INDEX  "Serial"

#define DES_SIZE 1024
#define PATH_SIZE 1024

#endif