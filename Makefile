GCC = gcc
FLAGS = -std=c99
TARGET = epoll_server.out

MAIN_SRC = main.c #NerCore.c
MAIN_OBJECT =$(MAIN_SRC:.c=.o)
MAIN_HEADER = NetCore.h

all : $(TARGET)

$(TARGET) : $(MAIN_OBJECT)
	$(GCC) -o $@ -lpthread $?
%.o : %.c
	$(GCC) -c $(FLAGS) $<

clean :
	rm -rf $(MAIN_OBJECT)
fclean : 
	rm -rf $(TARGET)
re : clean fclean all

.PHONY : all clean fclean re