obj-m += char_driver.o

all: module userapp_1 userapp_2 userapp_3 userapp_4 userapp_5 userapp_6 userapp_7 userapp_8 userapp_9

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

userapp_1: char_app_640x480.c
	gcc -o char_app_640x480 char_app_640x480.c -fopenmp -O3
userapp_2: char_app_800x600.c
	gcc -o char_app_800x600 char_app_800x600.c -fopenmp -O3
userapp_3: char_app_1024x768.c
	gcc -o char_app_1024x768 char_app_1024x768.c -fopenmp -O3
userapp_4: char_app_1152x864.c
	gcc -o char_app_1152x864 char_app_1152x864.c -fopenmp -O3
userapp_5: char_app_1366x768.c
	gcc -o char_app_1366x768 char_app_1366x768.c -fopenmp -O3
userapp_6: char_app_1280x800.c
	gcc -o char_app_1280x800 char_app_1280x800.c -fopenmp -O3
userapp_7: char_app_1440x900.c
	gcc -o char_app_1440x900 char_app_1440x900.c -fopenmp -O3
userapp_8: char_app_1600x1200.c
	gcc -o char_app_1600x1200 char_app_1600x1200.c -fopenmp -O3
userapp_9: char_app_1920x1080.c
	gcc -o char_app_1920x1080 char_app_1920x1080.c -fopenmp -O3
userapp_10: char_app_1280x1024.c
	gcc -o char_app_1280x1024 char_app_1280x1024.c -fopenmp -O3

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f char_app_640x480 char_app_1920x1080 char_app_1280x1024 char_app_800x600 char_app_1024x768 char_app_1152x864 char_app_1366x768 char_app_1280x800 char_app_1440x900 char_app_1600x1200 *.ppm


