obj-m += char_driver.o

all: module userapp

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

userapp: char_app.c
	gcc -o char_app char_app.c -fopenmp -O3

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f char_app *.ppm

