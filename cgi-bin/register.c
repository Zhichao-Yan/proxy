#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char* argv[],char* envp[])
{
    char content[1000];

    /* Make the response body */
    sprintf(content,"<html>\r\n");
    // head
    strcat(content,"<head>\r\n");
    strcat(content,"<meta charset=""utf-8"">\r\n<TITLE>成功</TITLE>\r\n");
    strcat(content,"<head/>\r\n");
    // body
    strcat(content,"<body>\r\n");
    strcat(content, "<h1> 恭喜您注册成功！！<h1/>\r\n");
    strcat(content,"<body/>\r\n");
    strcat(content,"<html/>\r\n");
  
    /* Send the HTTP response */
    printf("HTTP/1.0 200 OK\r\n");
    printf("Server: Tiny Web Server\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n");
    printf("Connection: keep-alive\r\n");
    printf("\r\n");
    printf("%s", content);
    fflush(stdout);

    fprintf(stderr,"用户提交的信息:\n");
    for(int i = 0; envp[i] != NULL; ++i)
    {
        fprintf(stderr,"    envp[%2d]: %s\n",i,envp[i]);
    }
    exit(0);
}