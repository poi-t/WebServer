#include <stdlib.h>
#include "redis.h"

int main() {
    char* method = NULL;
    method = getenv("REQUEST_METHOD");
    if(method != NULL && !strcmp(method, "GET")) {
        //��ȡredis�з�������Ϣ
        Redis* accept_count = new Redis();
        accept_count->connect("127.0.0.1", 6379);
        const char* count = accept_count->get("count").c_str();

        printf("<!DOCTYPE html>\n");
        printf("<html lang=\"en\">\n");
        printf("    <head>\n");
        printf("       <meta charset=\"UTF-8\">\n");
        printf("        <title>������</title>\n");
        printf("    </head>\n");
        printf("    <body>\n");
        printf("    <h1 style=\"text-align:center;\">��ǰ�ܷ�����</h1>\n");
        printf("    <div style=\"text-align:center;height��120px;font-size:28px\">%s</div>\n", count);
        printf("</body>\n");
        printf("</html>\n");
    }
    
    return 0;
}

