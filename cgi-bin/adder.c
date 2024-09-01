#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(void) 
{
    char *buf;
    char content[100], answer[100];
    int n1=0, n2=0,head_only = 0;
    /* Extract the two arguments */
    // homework 11.10
    if ((buf = getenv("value1")) != NULL)
    {
        n1 = atoi(buf);
    }
    if ((buf = getenv("value2")) != NULL)
    {
        n2 = atoi(buf);
    }
    if ((buf = getenv("head_only")) != NULL)
    {
        head_only = atoi(buf);
    }
    
    /* Make the response body */
    sprintf(content,"<html>\r\n");
    // head
    strcat(content,"<head>\r\n");
    strcat(content,"<meta charset=""utf-8"">\r\n<TITLE>adder</TITLE>\r\n");
    strcat(content,"<head/>\r\n");
    // body
    strcat(content,"<body>\r\n");
    strcat(content, "<h1> Welcome to Compute.com <h1/>\r\n");
    strcat(content,"<hr>\r\n");
    sprintf(answer, "<p><b>The answer is: %d + %d = %d <b/><p/>\r\n", n1, n2, n1 + n2);
    strcat(content,answer);
    strcat(content,"<body/>\r\n");
    strcat(content,"<html/>\r\n");
  
    /* Generate the HTTP response */
    printf("HTTP/1.0 200 OK\r\n");
    printf("Server: Tiny Web Server\r\n");
    printf("Content-length: %d\r\n", (int)strlen(content));
    printf("Content-type: text/html\r\n");
    printf("Connection: keep-alive\r\n");
    printf("\r\n");

    if(head_only == 1)
        return 0;
    printf("%s", content);
    fflush(stdout);
    exit(0);
}
/* $end adder */
