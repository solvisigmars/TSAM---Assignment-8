all: tsamserver tsamclient

CXXFLAGS = -Wall -std=c++23


tsamserver: server.cpp
	g++ ${CXXFLAGS} server.cpp -o tsamserver

tsamclient: client.cpp
	g++ ${CXXFLAGS} client.cpp -o tsamclient

clean:
	rm -f tsamserver tsamclient