# Compiler and flags
CC = gcc
CFLAGS = -Wall -pthread
LIBS = -lpthread

# Target executables
all: interface kitchen

# Interface (Restaurant Management)
interface: interface.c restaurant.h
	$(CC) $(CFLAGS) -o interface interface.c $(LIBS)

# Kitchen (Chef Process)
kitchen: kitchen.c restaurant.h
	$(CC) $(CFLAGS) -o kitchen kitchen.c $(LIBS)

# Clean build files
clean:
	rm -f interface kitchen
	rm -f /tmp/restaurant_stats.dat
	rm -f /tmp/restaurant_orders.log
	rm -f /tmp/restaurant_activity.log
	ipcrm -a 2>/dev/null || true

# Run interface
run-interface: interface
	./interface

# Run kitchen
run-kitchen: kitchen
	./kitchen

.PHONY: all clean run-interface run-kitchen