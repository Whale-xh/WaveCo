
CC = gcc
ECHO = echo 
ROOT_DIR = $(shell pwd)
OBJS_DIR = $(ROOT_DIR)/objs
BIN_DIR = $(ROOT_DIR)/bin
SRC_DIR  = $(ROOT_DIR)/src

FLAGs = -lpthread -O3 -ldl -I $(ROOT_DIR)/include

TARGET_LIB = libwaveco.a

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJS_DIR)/%.o,$(SRCS))

all : check_objs check_bin $(TARGET_LIB)

check_objs:
	if [ ! -d "objs" ]; then \
		mkdir -p objs;  \
	fi

check_bin:
	if [ ! -d "bin" ]; then \
		mkdir -p bin;   \
	fi

$(TARGET_LIB): $(OBJS)
	ar rcs $@ $^ 

$(OBJS_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) -c $^ -o $@ $(FLAGS)

clean:
	rm -rf $(OBJS) $(TARGET_LIB)