#
# Makefile for the kilo text editor.
# Visit http://viewsourcecode.org/snaptoken/kilo/ for more information.
#
# Author: Jarrod N. Bakker
# Date: 06/04/2016
#

kilo: kilo.c
	$(CC) kilo.c -o kilo -Wall -Wextra -pedantic -std=c99

clean:
	rm -f kilo
