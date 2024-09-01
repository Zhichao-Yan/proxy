#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main()
{
    char content[200],tmp[100];
    strcpy(content,"");
    strcpy(tmp,"");
    char *color = NULL;
    color = getenv("color");
    if (color == NULL)
        color = "blue";
    // 打印HTML内容
    strcat(content, "<!DOCTYPE html>\r\n");
    strcat(content,"<html>\r\n");
    strcat(content,"<head>\r\n");
    strcat(content,"<meta charset=\"utf-8\">\r\n");
    strcat(content,"<TITLE>你的颜色</TITLE>\r\n");
    // 动态添加样式标签
    sprintf(tmp, "<style>body { background-color: %s; }  </style>\r\n", color);
    strcat(content, tmp);
    strcat(content,"</head>\r\n");
    // sprintf(tmp,"<body style=\"background-color: %s;\">\r\n",color);
    // strcat(content,tmp);
    strcat(content,"<body>\r\n");
    sprintf(tmp,"<h1>This is %s</h1>\r\n",color);
    strcat(content,tmp);
    strcat(content,"</body>\r\n");
    strcat(content,"</html>\r\n");
    // fprintf(stderr,"%s",content);

    /* 输出响应行和响应首部*/
    printf("HTTP/1.0 200 OK\r\n");
    printf("Server: Tiny Web Server\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n");
    printf("Connection: keep-alive\r\n");
    printf("\r\n");

    /* 输出响应主体 */
    printf("%s", content);
    fflush(stdout);
    exit(0);
}