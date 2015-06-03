# For reference: http://www.gnu.org/software/make/manual/

CC = gcc
CFLAGS = -g -Wall -Wextra -Werror

all: myrouter

MYROUTER_SOURCES = \
  myrouter.c
# Add more stuff here if appropriate

MYROUTER_OBJECTS = $(subst .c,.o,$(MYROUTER_SOURCES))

myrouter: $(MYROUTER_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(MYROUTER_OBJECTS)

clean:
	rm -f *.o *.tmp routing-output*.txt myrouter
