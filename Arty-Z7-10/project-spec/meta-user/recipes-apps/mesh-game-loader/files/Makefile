APP = mesh-game-loader

# Add any other object files to this list below
APP_OBJS = main.o

all: build

clean:
	-rm -f $(APP) *.elf *.gdb *.o

build: $(APP)

$(APP): $(APP_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(APP_OBJS) $(LDLIBS)

