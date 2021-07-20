#ifndef REDIS_H
#define REDIS_H

#include <iostream>
#include <string.h>
#include <string>
#include <stdio.h>
#include <hiredis/hiredis.h>

using std::string;

class Redis {
public:
 
    Redis() {
        _connect = NULL;
        _reply = NULL;
    }
 
    ~Redis() {
        _connect = NULL;
        _reply = NULL;                
    }
 
    bool connect(string host, int port) {
        _connect = redisConnect(host.c_str(), port);
        if(_connect != NULL && _connect->err) {
            return false;
        }
        return true;
    }
 
    string get(string key) {
        _reply = (redisReply*)redisCommand(_connect, "GET %s", key.c_str());
        string str = _reply->str;
        freeReplyObject(_reply);
        return str;
    }
 
    void set(string key, string value) {
        redisCommand(_connect, "SET %s %s", key.c_str(), value.c_str());
    }
    
    void set(string key, string timeout, string value) {
        redisCommand(_connect, "SET %s %s %s", key.c_str(), timeout.c_str(), value.c_str());
    }
    
    void incr(string key) {
        redisCommand(_connect, "incr %s", key.c_str());
    }

    void setnx(string key, string value) {
        redisCommand(_connect, "setnx %s %s", key.c_str(), value.c_str());
    }

private:
    redisContext* _connect;
    redisReply* _reply;
 
};


#endif
