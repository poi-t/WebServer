server : *.cpp
	g++ http_conn.cpp server.cpp -o server -L/usr/local/lib/ -pthread -lhiredis -std=c++11