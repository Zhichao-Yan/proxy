#include "proxy.h"

#define LISTENQ  1024
#define SBUFSIZE 16
#define QUEUE_SIZE 100
#define MINI_THREADS 4
#define MAXBUF 8192
#define MAXLINE 8192
#define localhost "172.16.153.130"
#define localport "4000"

sbuf_t sbuf; // 缓冲
pthread_cond_t cond = PTHREAD_COND_INITIALIZER; // 条件变量

int main(int argc,char **argv)
{
    signal(SIGCHLD,signal_sigchld);
    signal(SIGPIPE, ignore_sigpipe);
    if(argc != 2)
    {
        fprintf(stderr,"usage: %s <port>\n",argv[0]);
        exit(1);
    }
    int listenfd,clientfd;
    listenfd = open_listenfd(argv[1]);
    if(listenfd < 0)
    {
        exit(1);
    }
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    sbuf_init(&sbuf,SBUFSIZE);  // 初始化缓冲
    pthread_t tid; 
    pthread_create(&tid,NULL,manager,NULL);     // 创建管理者线程
    while(1)
    {
        clientfd = accept(listenfd,(struct sockaddr*)&client_addr,&addrlen);
        if(clientfd < 0)
        {
            perror("accept error");
            break;
        }
        // transaction(clientfd);   // 处理事务
        sbuf_insert(&sbuf,clientfd);
    } 
    close(listenfd);
    return 0;
}


/* set nonblocking flag */
static int set_nonblocking(int fd) 
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) 
    {
        perror("Failed setting file descriptor status flag");
        return -1;
    }
    return 0;
}

void transaction(int clientfd)
{
    conn_mode_t mode = HTTP_MODE;
    int serverfd = -1; 
    /* 设置为非阻塞 */
    if (set_nonblocking(clientfd) < 0)
    {
        return;
    }
    /* 初始化缓冲 */
    rio_t rio;
    rio_readinitb(&rio,clientfd);

    fd_set read_set,ready_set;
    FD_ZERO(&read_set);
    FD_SET(clientfd,&read_set);
    int max_fd = clientfd;

    struct timeval timeout;
    timeout.tv_sec = 5; // 等待时间10秒
    timeout.tv_usec = 0; // 微秒，0表示不使用微秒级超时

    while(1)
    {
        ready_set = read_set;
        int activity = select(max_fd + 1, &ready_set, NULL, NULL, &timeout);
        if(activity < 0)
        {
            if(errno !=  EINTR)
            {
                perror("Select error");
                break;
            } 
        }else if(activity == 0)
        {

            perror("Timeout");
            break;

        }else if(activity > 0)
        {
            if (FD_ISSET(clientfd, &ready_set)) // 客户端可以读了
            {
                char buf[MAXBUF] = {'\0'};
                int rc = rio_readnb(&rio,buf,MAXBUF);
                if(rc > 0)
                {   
                    if(mode == HTTP_MODE)
                    {
                        printf("---->Request(%d)<----\n",clientfd);       
                        printf("%s\n",buf);
                        char *line = NULL,*headers = NULL,*body = NULL,*ptr = NULL;
                        ptr = strpbrk(buf,"\r\n");
                        if(ptr)
                        {
                            *(ptr) = '\0';
                            headers = ptr + 2;
                            line = buf;
                        }
                        ptr = strstr(headers,"\r\n\r\n");
                        if(ptr)
                        {
                            *(ptr) = '\0';
                            body = ptr + 4;
                        }
                        char host[100] = {'\0'},port[10] = {'\0'};
                        parse_headers(headers,"Host",host);
                        parse_host(host,port);
                        if(strcmp(host,localhost) == 0 && strcmp(port,localport) == 0)
                        {
                            parse_local_request(clientfd,line,headers,body);
                        }else{
                            serverfd = connect_to_server(host,port);
                            if(serverfd > 0)
                            {
                                set_nonblocking(serverfd);
                                FD_SET(serverfd, &read_set);
                                if (serverfd > max_fd) 
                                {
                                    max_fd = serverfd;
                                }
                                mode = TUNNEL_MODE;
                                send_conn_response(clientfd);
                            }else
                            {
                                send_error_response(clientfd,"HTTP/1.1","503","Service Unavailable","Failed connecting to the Server",host,0);
                            }
                        }
                    }else if(mode == TUNNEL_MODE)
                    {
                        if (rio_writen(serverfd, buf, rc) < 0) 
                        {
                            perror("Failed write to serverfd");
                            break;
                        }
                    }
                }else if( rc < 0)
                {
                    perror("Failed reading from cliendfd");
                    break;
                }else if(rc == 0)
                {
                    perror("clientfd close");
                    break;                  
                }
            }
            // 处理代理转发
            if (serverfd > 0 && FD_ISSET(serverfd, &ready_set) && mode == TUNNEL_MODE) 
            {
                /* 初始化缓冲 */
                rio_t rio;
                rio_readinitb(&rio,serverfd);
                char buf[MAXBUF] = {'\0'};
                int rc = rio_readnb(&rio,buf,MAXBUF);
                if(rc > 0)
                {
                    if(rio_writen(clientfd,buf,rc) < 0)
                    {
                        perror("Failed writing to clientfd");
                        break;
                    }
                }else{
                    perror("Failed reading from serverfd");
                    /* 读取失败，重新切换回HTTP_MODE */
                    FD_CLR(serverfd, &read_set);
                    max_fd = clientfd;
                    close(serverfd);
                    serverfd  = -1;
                    mode = HTTP_MODE;
                }
            }
        }
    }
    close(clientfd);
    if(serverfd > 0)
        close(serverfd);
    return;
}

/* 连接目的服务器 */
int connect_to_server(char *host,char *port)
{
    struct addrinfo *result,*p;
    struct addrinfo hints;
    int serverfd;
    memset(&hints, 0, sizeof(struct addrinfo)); //  只能设置某些字段，只好先清空hints
    /* hints提供对getaddrinfo返回的listp指向的套接字链表更好的控制 */
    hints.ai_family  = AF_INET;
    hints.ai_socktype = SOCK_STREAM;      // 对每个地址，规定只返回SOCK_STREAM套接字
    /*  hints.ai_flags 是掩码 */
    hints.ai_flags = AI_ADDRCONFIG;
    int rc;
    if ((rc = getaddrinfo(host, port, &hints, &result)) != 0) 
    {
        fprintf(stderr, "getaddrinfo failed (host-%s port-%s): %s\n", host,port, gai_strerror(rc));
        return -2;
    }
    /* 遍历列表，找到套接字，直到成功 */
    for (p = result; p; p = p->ai_next) 
    {
        /* 直接用返回addrinfo中的数据结构作为参数调用socket函数 */
        if ((serverfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) 
            continue;  // 失败了，继续链表中下一个套接字地址

        if(connect(serverfd,p->ai_addr,p->ai_addrlen) == 0)
        {
            break;
        }
        if (close(serverfd) < 0) 
        {   
            fprintf(stderr, "serverfd close failed: %s\n", strerror(errno));
            freeaddrinfo(result);
            return -1;
        }  
    }
    freeaddrinfo(result);
    if (!p)     // p为空，意味着链表检查完，没有套接字地址绑定描述符成功
        return -1;
    return serverfd;
}


/* 工作线程 */
void* worker(void *arg)
{
    pthread_detach(pthread_self());
    while(1)
    {
        int oldstate;
        /* 
        *    取消状态再次变成PTHREAD_CANCEL_DISABLE
        *    取消请求会被挂起，这意味这在下面的网络通信中
        *    线程不会被取消，直到取消状态再次变成PTHREAD_CANCEL_ENABLE
        *  */
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&oldstate);   // 线程暂时不可取消   
        int clientfd = sbuf_remove(&sbuf);
        transaction(clientfd);   // 处理事务
        pthread_setcancelstate(oldstate,NULL);  // 取消状态再次变成PTHREAD_CANCEL_ENABLE，可以接受取消
        pthread_testcancel();   // 设置取消点，线程在此检查是否有取消请求，如果有，就会pthread_exit()
    } 
}


/* 管理者线程例程 */
void* manager(void *arg)
{
    /* 初始化线程id队列 */
    queue q;
    init_queue(&q,QUEUE_SIZE);

    pthread_t tid;
    /* 初始创建MINI_THREADS个线程*/
    for(int i = 0; i < MINI_THREADS; ++i)
    {
        pthread_create(&tid,NULL,worker,NULL);
        queue_push(&q,tid);
    }
    unsigned long cnt = MINI_THREADS;   // cnt记录当前的线程数量
    while(1)
    {
        pthread_mutex_lock(&(sbuf.mutex));
        /* 
        *   已知当缓冲区为空或者为满的时候，sbuf.front == sbuf.rear
        *   如果不满足该条件，则释放互斥量，并且继续睡眠等待。
        *   当收到满或者空的信号时醒过来，重新获得锁，并且检查条件
        *   如果不满足循环，则退出循环
        *  */
        while(sbuf.front != sbuf.rear)  
        {
            pthread_cond_wait(&cond,&(sbuf.mutex));
        }
        if(sbuf.count == 0)
        {
            // 如果cnt比规定最小的线程数量还大,并且是其二次幂，则继续减小线程数量，否则即算了
            if(cnt > MINI_THREADS)
            {
                unsigned long i = cnt * 0.5;
                while(i > 0)
                {
                    tid = queue_pop(&q);    // 弹出线程tid
                    --i;            
                    pthread_cancel(tid);    // 取消线程tid
                }
                cnt = cnt * 0.5;
            }
        }else if(sbuf.count == sbuf.n)
        {
            // double线程数量
            for(unsigned long i = 0; i < cnt; ++i)
            {
                pthread_create(&tid,NULL,worker,NULL);
                queue_push(&q,tid);
            }
            cnt = 2 * cnt;
        }
        pthread_mutex_unlock(&(sbuf.mutex));
    }
}

/* 从url字符串中提取协议方案/主机名端口/路径 */
void parse_url(char *url,char *scheme,char *host,char *path)
{
    char *start = url,*ptr = NULL;
    ptr = strstr(start,"://");
    if(!ptr)    // url中没有scheme
    {
        strcpy(scheme,"");  // 返回scheme空字符串
    }else{
        int len = ptr - start;
        strncpy(scheme,start,len);
        scheme[len] = '\0';
        start = ptr + 3; 
    }

    int len = 0;
    ptr = strchr(start,'/');
    if(!ptr)    // 如果没有路径
    {
        strcpy(path,"");  // 返回空串
        len = strlen(start);
    }else{
        strcpy(path,ptr); // 从/处开始复制，直到结束
        len = ptr - start;
    }

    strncpy(host,start,len);
    host[len] = '\0';

    return;
}

/* 从headers中解析出某个header */
void parse_headers(const char *headers,const char *name,char *value)
{
    char *start, *end;
    // 在输入字符串中查找name的位置
    start = strstr(headers, name);
    if (start != NULL) 
    {
        // 移动指针到name对应值的开始位置
        start += strlen(name);
        start += 2;
        // 查找name值结尾的\r\n
        end = strstr(start, "\r\n");
        if (end != NULL) 
        {
            int length = end - start;
            strncpy(value, start, length);
            value[length] = '\0'; // 确保字符串以空字符结尾
        }else{
            strcpy(value,start);
        }
    }else{  // 没找到
        strcpy(value,"");
    }
    return;
}

/* 尝试从主机中分割出端口号 */
void parse_host(char *host,char *port)
{
    char *ptr;
    ptr = strchr(host,':');
    if(ptr)
    {
        strcpy(port,ptr + 1);
        *ptr = '\0';
    }else{
        strcpy(port,"");
    }
    return;
}

/* 从path中分割出文件名和参数名 */
void parse_path(char *path,char *filename,char *args)
{
    char *ptr = strchr(path,'?');// 返回？在path中第一个出现的位置
    // 找到'?'
    if(ptr)
    {
        // 把链接中的参数部分拷贝进去
        strcpy(args,ptr+1);
        *ptr = '\0';
    }else
        strcpy(args,"");//如果没有找到？,则没有参数，置空
    strcpy(filename,".");
    strcat(filename,path);
    if(strcmp(filename,"./") == 0)
    {
        strcat(filename,"html/index.html");//拼接默认对主页html文件
    }
    return;
}

/* 解析本地请求 */
void parse_local_request(int clientfd,char *line,char *headers,char *body)
{
    char *method = NULL,*url = NULL,*version = NULL,*ptr = NULL;
    /* 获取method */
    method = line;
    ptr = strchr(line,' ');
    *ptr = '\0';
    ptr = ptr + 1;
    /* 获取url */
    url = ptr;
    ptr = strchr(ptr,' ');
    *ptr = '\0';
    ptr = ptr + 1;
    /* 获取version */
    version = ptr;
    version = NULL;
    /* 从url中抽取方案、站点、路径 */
    char scheme[10] = {'\0'},path[200] = {'\0'},station[100] = {'\0'};
    parse_url(url,scheme,station,path);
    local_service(clientfd,method,path,body);
    return;
}

/* 解析http请求报文 */
void parse_request1(int clientfd,const char *buf)
{
    char request[MAXBUF];
    strcpy(request,buf);
    char *line = NULL,*headers = NULL,*body = NULL,*ptr = NULL;
    /* 获得line和headers */
    ptr = strpbrk(request,"\r\n");
    if(ptr)
    {
        *ptr = '\0';
        *(ptr + 1) = '\0';
        headers = ptr + 2;
        line = request;
    }
    /* 获得body */
    ptr = strstr(headers,"\r\n\r\n");
    if(ptr)
    {
        *(ptr + 2) = '\0';
        *(ptr + 3) = '\0';
        body = ptr + 4;
    }
    char *method = NULL,*url = NULL,*version = NULL;
    /* 获取method */
    method = line;
    ptr = strchr(line,' ');
    *ptr = '\0';
    ptr = ptr + 1;
    /* 获取url */
    url = ptr;
    ptr = strchr(ptr,' ');
    *ptr = '\0';
    ptr = ptr + 1;
    /* 获取version */
    version = ptr;
    /* 从url中抽取方案、站点、路径 */
    char scheme[10] = {'\0'},path[200] = {'\0'},host0[100] = {'\0'};
    char host[100] = {'\0'},port[10] = {'\0'};
    parse_url(url,scheme,host0,path);
    /* 如果协议是HTTP/1.1 */      
    if(strcmp(version,"HTTP/1.1") == 0)               
    {
        parse_headers(headers,"Host",host);
    }
    /* 如果协议是HTTP/1.0 或者上面没有提取到host */
    if(strcmp(version,"HTTP/1.0") == 0 || strcmp(host,"") == 0)
    {
        strcpy(host,host0);  // 从host0复制到host
    }
    parse_host(host,port); 
    /* 如果分割得到端口为空,则根据方案来推测 */
    if(strcmp(port,"") == 0)              
    {
        if(strcmp(scheme,"") == 0)
        {
            strcpy(port,"443");
        }else if(strcasecmp(scheme,"http") == 0)
        {
            strcpy(port,"80");
        }else if(strcasecmp(scheme,"https") == 0)
        {
            strcpy(port,"443");
        }
    }

    if(strcmp(host,localhost) == 0 && strcmp(port,localport) == 0)
    {
        local_service(clientfd,method,path,body);
    }else{
        
    }
    return;
}

/* 发送响应 */
void send_conn_response(int clientfd)
{
    char buf[MAXLINE] = {'\0'};
    strcpy(buf,"HTTP/1.1 200 Connection Established\r\n");
    strcat(buf,"Server: The Tiny webserver\r\n");
    strcat(buf,"Proxy-Connection: keep-alive\r\n");
    strcat(buf,"Connection: keep-alive\r\n");
    strcat(buf,"\r\n");
    printf("---->Response<----\n");
    printf("%s",buf);
    if(rio_writen(clientfd,buf,strlen(buf)) < 0)
        perror("write error");
    return;
}

/* 给客户端发送响应 */
void send_response(int fd,const char *line,const char *header,const char *body)
{
    char buf[MAXBUF] = {'\0'};
    if(line != NULL)
    {
        strcpy(buf,line);
    }
    if(header != NULL)
    {
        strcat(buf,header);
    }
    if(body != NULL)
    {
        strcat(buf,body);
    }
    rio_writen(fd,buf,strlen(buf));
    return;
}

/* 给客户端返回错误响应报文 */
void send_error_response(int fd,char *version,const char *code,const char *status,const char *msg,const char *cause,int head_only)
{
    char tmp[MAXBUF] = {'\0'};  // 临时字符数组
    // 错误报文实体主体
    char body[MAXBUF] = {'\0'};
    strcpy(body, "<html>\r\n");           // 开始HTML文档
    strcat(body, "<head><title>Server Error</title></head>\r\n");
    strcat(body, "<body>\r\n");           // 开始body部分
    strcat(body, "<em>The Tiny webserver</em>\r\n"); // 添加标题
    strcat(body, "<hr>\r\n");    // 水平线
    sprintf(tmp, "%s: %s\r\n", code, status); // 格式化错误代码和状态
    strcat(body, tmp);
    sprintf(tmp, "<p>%s: %s</p>\r\n", msg, cause); // 格式化错误消息和原因
    strcat(body, tmp);
    strcat(body, "</body>\r\n</html>\r\n"); // 结束body和HTML文档

    // 响应报头： Server: Tiny Web Server
    char header[MAXBUF] = {'\0'};
    strcpy(header,"Server: Tiny Web Server\r\n");
    // 响应报头：Content-type
    strcat(header,"Content-type: text/html\r\n");
    // 响应报头：Content-length
    strcpy(tmp,"");
    sprintf(tmp,"Content-length:%d\r\n",(int)strlen(body));
    strcat(header,tmp);
    // 响应报头：Connection: close
    strcat(header,"Connection: keep-alive\r\n");
    // 跟随一个终止报头的空行
    strcat(header,"\r\n");

    // 响应行： HTTP版本 状态码 状态消息
    char line[MAXBUF] = {'\0'};
    sprintf(line,"%s %s %s\r\n",version,code,status);

    printf("---->Response<----\n");
    printf("%s%s",line,header);

    if(head_only == 1)
    {
        send_response(fd,line,header,NULL);
    }else{
        send_response(fd,line,header,body); 
    }
    return;
}
/* 根据文件名，获取文件类型 */
static void get_filetype(char *filename,char *filetype)
{
    if(strstr(filename,".html"))
        strcpy(filetype,"text/html");
    else if(strstr(filename,".gif"))
            strcpy(filetype,"imgage/gif");
    else if(strstr(filename,".png"))
            strcpy(filetype,"imgage/png");
    else if(strstr(filename,".jpeg"))
            strcpy(filetype,"imgage/jpeg");
    else if(strstr(filename,".jpg"))
            strcpy(filetype,"imgage/jpg");
    else if(strstr(filename,".JPG"))
            strcpy(filetype,"image/JPG");
    else
        strcpy(filetype,"text/plain");
    return;
}

/* 发送静态文件（mmap映射） */
void serve_static(int fd,char *filename,char *args,int head_only)
{
    int srcfd = open(filename,O_RDONLY,0);  // 打开一个文件filename并返回文件描述符
    struct stat state;
    if (fstat(srcfd, &state) < 0) 
    {
        perror("文件状态获取失败\n");
        return;
    }
    size_t filesize = state.st_size;
    /* 映射到body */
    char *body = mmap(NULL,filesize,PROT_READ,MAP_PRIVATE,srcfd,0);

    /* 构造响应报文首部*/
    char header[MAXBUF],tmp[MAXLINE],filetype[100];
    strcpy(header,"Server: Tiny Web Server\r\n");
    // 响应报头Content-length
    sprintf(tmp,"Content-length: %ld\r\n",filesize);
    strcat(header,tmp);
    // 获取文件类型，用于填充响应报头Content-type
    get_filetype(filename,filetype);    
    sprintf(tmp,"Content-type: %s\r\n",filetype);
    strcat(header,tmp);  
    // 响应报头Connection
    strcat(header,"Connection: keep-alive\r\n");
    // 响应报头结尾的回车换行符
    strcat(header,"\r\n");

    // 响应行： HTTP版本 状态码 状态消息
    char line[MAXBUF] = {'\0'};
    strcpy(line,"HTTP/1.1 200 OK\r\n");

    printf("---->Response<----\n");
    printf("%s",line);
    printf("%s",header);
    send_response(fd,line,header,NULL);

    if(head_only == 0)
    {
        // 从body开始写入filesize个字节到fd
        if(rio_writen(fd,body,filesize) < 0)
        {
            printf("write error\n");
        }  
    }
    close(srcfd);   // 关闭文件描述符
    munmap(body,filesize);      // 在进程地址空间中解除一个映射关系
    return;
}


/* 解析查询参数，放入result中 */
void parse_query_string(char* str,char** result) 
{
    int i = 0;
    char *ptr = NULL;
    while((ptr = strchr(str,'&')))
    {
        result[i++] = str;
        *ptr = '\0';
        str = ptr + 1;
    }
    result[i++] = str;
    result[i] = NULL;
}

/* 服务动态文件 */
void serve_dynamic(int fd,char *filename,char *cgi_args,int head_only)
{
    // 创建子进程
    if(fork() == 0)
    {
        char head[20];
        sprintf(head,"&head_only=%d",head_only);
        strcat(cgi_args,head);
        // 子进程参数列表
        char *envp[MAXLINE];
        parse_query_string(cgi_args,envp);
        char *argv[] = {filename,NULL};
        //将子进程的标准输出重定向到fd描述符
        //这个的fd为客户端连接套接字文件描述符
        dup2(fd,STDOUT_FILENO);
        // 指向filename指向的动态文件
        // argv 参数字符串指针数组
        // envp 环境变量字符串指针数组
        execve(filename,argv,envp);
    }
}

/* homework-11.12 POST方法  */
void method_post(int fd,char *filename,char *body)
{
    serve_dynamic(fd,filename,body,0);
    return;
}

/* head方法（homework 11.11)*/
void method_head(int fd,char *filename,char *args)
{
    method_get(fd,filename,args,1);
    return;
}

/* get方法 */
void method_get(int fd,char *filename,char *args,int head_only)
{
    struct stat state;   // 获取文件的状态
    /* 有没有参数不能决定是否执行动态请求，静态请求也可以有参数，关键在于文件是否是可执行 */
    int flag = 0; // 标志：是否为静态请求。默认情况为flag为0，表示静态请求，
    // 将filename的文件属性信息保存到state
    if(stat(filename,&state) < 0)    // filename指定路径的文件/目录不存在，返回-1
    {
        // 发生给客户端404错误报文
        send_error_response(fd,"HTTP/1.1","404","Not found","Server couldn't find the file",filename,head_only);
        return;
    }
    if(strstr(filename,"cgi-bin"))  // 动态文件夹，转变为动态请求
    {
        flag = 1;  // 动态请求
    }
    if(S_ISREG(state.st_mode))   // 普通文件，而不是目录文件或者套接字文件
    {
        if(flag == 0) 
        {
            // 静态文件，但是用户没有读取权限
            if(!(S_IRUSR&state.st_mode))
            {
                send_error_response(fd,"HTTP/1.1","403","Forbidden","Server couldn't read the file",filename,head_only);
                return;
            }
            serve_static(fd,filename,args,head_only);
        }else{
            // 动态文件，但是没有执行权
            if(!(S_IXUSR&state.st_mode))
            {
                send_error_response(fd,"HTTP/1.1","403","Forbidden","Server couldn't run the file",filename,head_only);
                return;
            }
            serve_dynamic(fd,filename,args,head_only);
        }
    }else{
            send_error_response(fd,"HTTP/1.1","400","Bad Request","This is not a regular file",filename,head_only);
    }
    return;
}

/* 访问本地主机服务 */
void local_service(int clientfd,char *method,char *path,char *body)
{
    char filename[1000],args[100];
    parse_path(path,filename,args);     // 从路径中解析得到文件名和参数
    
    if(strcasecmp(method,"GET") == 0)
    {
        /* 执行get方法 */
        method_get(clientfd,filename,args,0);
    }else if(strcasecmp(method,"HEAD") == 0)
    {
        /* 执行head方法 */
        method_head(clientfd,filename,args);
    }else if(strcasecmp(method,"POST") == 0)
    {
        /* 执行post方法 */            
        method_post(clientfd,filename,body);
    }else{
        send_error_response(clientfd,"HTTP/1.1","501","Not implemented","Server doesn't implemented this method",method,0);
    }
}



/* homework 11.8 */
void signal_sigchld(int sig)
{
    pid_t pid = waitpid(-1,NULL,0);
    printf("---->Response<----\n");
    printf("子进程%d结束\n\n",pid);
    return;
}
/* homework 11.13 */
void ignore_sigpipe(int sig) 
{
    printf("连接被客户端提前关闭\n");
    return;
}


/* open listen socket */
int open_listenfd(char *port)
{
    int listenfd = socket(AF_INET,SOCK_STREAM,0);
    if(listenfd < 0)
    {
        perror("socket error");
        return -1;
    }    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    unsigned short port_num = strtoul(port,NULL,0);
    server_addr.sin_port = htons(port_num);
    /* Eliminates "Address already in use" error from bind */
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        close(listenfd);
        perror("bind error"); 
        return -1;
    }    
    if (listen(listenfd, LISTENQ) < 0) // 出错的话返回-1
    {
        close(listenfd); 
        perror("listen error");   
	    return -1;
    }
    return listenfd;
}