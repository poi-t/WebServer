all : server client
server : server.cpp
	g++ -pthread -o server server.cpp

client : client.cpp
	g++ -o client client.cpp
