server : *.cpp
	g++ http_conn.cpp server.cpp -o server -pthread -std=c++11