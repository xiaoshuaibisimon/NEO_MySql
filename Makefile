src = $(wildcard *.c)
targets = $(patsubst %.c, %.o, $(src))

CC = arm-linux-gcc
CFLAGS = -I/usr/local/include/ -I/usr/local/myconnector/include/  -I/home/zhenjun/Downloads/src/  -L/opt/FriendlyARM/toolchain/4.9.3/arm-cortexa9-linux-gnueabihf/lib/ -L/usr/local/myconnector/lib/ -lmysqlclient -lm -ldl -lstdc++ -lpthread -lrt  -L/usr/local/lib/ -lfahw
all:app

app:$(targets)
	$(CC) test.o uart.o -o app $(CFLAGS)
  
%.o:%.c
	$(CC) -c  $< -Wall -g $(CFLAGS) 

.PHONY:clean all
clean:
	-rm -rf $(targets)  app
