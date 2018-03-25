all: mmaplib migration

CXX=g++
CC=gcc
LIBS=-lpthread -lrt -lhoard

mmaplib: mmaplib.c
	$(CC) -fpic -c mmaplib.c
	$(CC) -fpic -shared -o libmmap.so mmaplib.o

migration: migration.c
	$(CC) -fpic -c migration.c $(LIBS) -D_STOPMIGRATION #-D_STOPONDEMAND
	$(CC) -fpic -shared -o libmigration.so migration.o $(LIBS)

install:
	sudo cp *.so /usr/local/lib
	sudo cp *.so /usr/lib
	sudo cp *.h /usr/local/include
	sudo cp *.h /usr/include
	sudo cp *.h /usr/lib/

clean:
	rm -rf *.o
	rm -rf *.so

