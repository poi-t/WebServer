httpServer : server cgi_redis

server : *.cpp
	g++ http_conn.cpp server.cpp -o server -L/usr/local/lib/ -pthread -lhiredis -std=c++11

cgi_redis : cgi_redis.cpp
	g++ cgi_redis.cpp -o cgi_redis -L/usr/local/lib/ -lhiredis
	mv cgi_redis resources/