CXXFLAGS=-g -Wall

readwrite: readwrite.cpp readwrite.h option.cpp

clean:
	rm -rf *.o readwrite
