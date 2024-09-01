#include "rio.h"


void rio_readinitb(rio_t *rp, int fd) 
{
    rp->rio_fd = fd;    // 关联缓冲区和文件描述符
    rp->rio_cnt = 0;    // 缓冲区未读字节数量
    rp->rio_bufptr = rp->rio_buf;   // 下一个未读字节位置
}

/****************************************
 * The Rio package - Robust I/O functions
 ****************************************/

/* rio_readn Robustly read n bytes (unbuffered)
 * 在网络编程中，因为较长的网络延迟，会导致系统调用read()返回不足值。
 * 1. 返回正数字节数
 *      1. 遇到EOF，返回不足值，但是不一定为0，可能是小于n的字节数目
 *      2. 返回足值n
 * 2. 出错会返回-1
 * 特点：
 * 1. 因为网络延迟，遇到read()返回除开EOF外的不足值，rio_readn()能够自动重启read()继续读取，
 * 2. read()如果被信号处理程序中断，rio_readn()能够自动重启read()，具有较好的移植性
 * 3. 对同一个描述符，可以任意交错调用rio_readn和rio_writen
 * 总结：能够处理网络延迟，并且碰到信号处理程序中断能够重启，因此相比read函数具有更好的鲁棒性
 * */
ssize_t rio_readn(int fd, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;
    while (nleft > 0) 
    {
    /*  系统调用read如果被信号中断，信号处理函数执行完成后，read不再继续，
     *  而是返回-1，并且设置错误代码errno == EINTR。
     *  系统调用read被中断通常不会导致文件描述符的状态或数据流的状态发生改变。
     *  也就是说，被中断的read调用不会影响文件描述符指向的文件或流的当前读取位置。
     *  这里遇到errno == EINTR的情况，会设置nread = 0，nleft保存不变，从而重新进入循环
     *  这里封装了read，能够自动重启中断的系统调用函数read，包装函数更具有鲁棒性
     * */
        if ((nread = read(fd, bufp, nleft)) < 0) 
        {
            if (errno == EINTR) /* Interrupted by sig handler return */
                nread = 0;      /* and call read() again */
            else
                return -1;      /* errno set by read() */ 
        } 
        else if (nread == 0)    // 当read函数遇到EOF，会返回0，因此如果nread为0，表面遇到EOF
            break;              /* EOF */
        nleft -= nread;     // 遇到read()返回不足值
        bufp += nread;
    }
    return (n - nleft);         /* Return >= 0 */
}

/* rio_writen Robustly write n bytes (unbuffered)
 * 1. 出错返回-1
 * 2. 绝对不返回不足值
 * 
 * */
ssize_t rio_writen(int fd, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;
    while (nleft > 0) 
    {
        if ((nwritten = write(fd, bufp, nleft)) <= 0) 
        {
            // 道理同rio_readn()
            if (errno == EINTR)  /* Interrupted by sig handler return */
            {
                nwritten = 0;    /* and call write() again */
            }    
            else if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                /* 在非阻塞模式的 I/O 操作中，如果试图读取或写入数据，
                但当前没有足够的空间可写，write返回-1，返回这2种错误。 */
                nwritten = 0;
            }
            else 
            {
                /* 其他的错误 */
                return -1;       /* errno set by write() */
            }
        }
        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}

/* 
 * rio_read - This is a wrapper for the Unix read() function that
 *    transfers min(n, rio_cnt) bytes from an internal buffer to a user
 *    buffer, where n is the number of bytes requested by the user and
 *    rio_cnt is the number of unread bytes in the internal buffer. On
 *    entry, rio_read() refills the internal buffer via a call to
 *    read() if the internal buffer is empty.
 * 1. 返回读取的字节数：可能是一个不足值 
 *      1. 返回不足值：
 *          1. 返回0：read遇到了EOF
 *          2. 其他大于0的不足值：read读取了比n小的字节数，或者内部缓冲剩余字节数小于n
 *      2. 返回足值n
 * 2. 返回-1:表示遇到了非信号处理程序中断的其他错误
 * 3. 能够处理信号处理程序带来的中断并重新启动read()
 */
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;
    while (rp->rio_cnt <= 0)    // 缓冲区中未读的字节树为小于等于0
    {  
        /* Refill if buf is empty 重新读取并填充内部缓冲区 */
	    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));
        if (rp->rio_cnt < 0) 
        {
            /*  
            * 如果是被信号处理程序中断，read返回-1，并且设置全家错误变量为EINTR
            * 此时rp->rio_cnt = -1，会重新循环进行读取
            * 如果不是信号处理程序中断，则为其他错误，直接返回-1
            * */
            if (errno != EINTR)     // 如果不是EINTR，则表示为read并非信号中断，返回-1
                return -1;
            
        }
	    else if (rp->rio_cnt == 0)  // EOF导致read返回不足值0，直接返回0
	        return 0;
	    else        
	        rp->rio_bufptr = rp->rio_buf; // 成功读取了rp->rio_cnt个字节并设置rp->rio_bufptr
    }
    /* rp->rio_cnt可能远大于需要的n，但是也可能小于n（因为read读取了不足值）*/
    cnt = n;          // cnt取cnt和rp->rio_cnt的较小者
    if (rp->rio_cnt < n)   
	    cnt = rp->rio_cnt;
    /* 从内部缓冲rp->rio_bufptr拷贝cnt个字节到用户缓冲usrbuf */
    memcpy(usrbuf, rp->rio_bufptr, cnt);    
    rp->rio_bufptr += cnt;  // 更新rp->rio_bufptr位置，以备下次读取
    rp->rio_cnt -= cnt; // 更新缓冲区未读字节数量，以备下次读取
    return cnt; // 返回从缓冲区复制出来的字节数目
}


/*
 * rio_readnb - Robustly read n bytes (buffered)
 * 带内部缓冲的读函数（具有鲁棒性）特点：
 * 1. 从内部缓冲区复制n个bytes数据，如果内部缓冲区空了，能够自动填充
 * 2. 适合既包含文本又包含二进制数据的文件：使用rio_readnb读取二进制，使用rio_readlineb读取文本
 * 3. 对同一描述符和内部缓冲，rio_readlineb和rio_readnb可以交错使用
 * 4. 如果描述符为阻塞状态，如果没有读完n个字节，read会阻塞
 * 返回值
 * 1. 返回-1，出现非信号中断的错误
 * 2. 返回正数字节数
 *      1. 返回不足值：因为遇到EOF，返回不足值，但是不一定为0，而是已经读取的小于n的字节数
 *      2. 返回足值n 
 */
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;
    while (nleft > 0) 
    {
        if ((nread = rio_read(rp, bufp, nleft)) < 0) 
        {
            /* 在非阻塞模式的 I/O 操作中，如果试图读取或写入数据，但当前没有足够的数据可读或空间可写，就会返回这个错误 */
            if(errno == EAGAIN || errno == EWOULDBLOCK)     // 暂时没有数据可读
                break;
            else
                return -1;      // 遇到非信号处理程序中断的错误，rio_read中已经帮你处理了信号中断的错误
        }        
        else if (nread == 0)
            break;              /* 遇到EOF返回0 */
        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft);         /* return >= 0 */
}


/* 
 * rio_readlineb - Robustly read a text line (buffered)
 * 带缓冲区的读行函数（鲁棒性）
 * 从内部缓冲区复制一个文本行
 * 适合从文本文件每次读取一个文本行
 * 对同一描述符和缓冲，rio_readlineb和rio_readnb可以交错使用
 * 1. 返回-1，出现非信号中断的错误
 * 2. 返回0，遇到EOF
 * 3. 返回正数，文本行个数/maxlen-1
 */
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    int n, rc;
    char c, *bufp = usrbuf;
    for (n = 1; n < maxlen; n++)    // 最多读取maxlen-1个字符，最后一个留给'\0'
    { 
        if ((rc = rio_read(rp, &c, 1)) == 1) 
        {
	        *bufp++ = c;
	        if (c == '\n')  // 提前读到换行符，说明本行已经结束
            {
                n++;
     		    break;
            }
	    }else if (rc == 0) 
        {
	        if (n == 1)
                return 0; // 遇到EOF，并且还没有读数据，直接返回0
            else
                break;   // 遇到EOF，并且已经读了一些数据，跳出循环
	    }else
	        return -1;	  // 遇到非信号处理程序中断的错误，rio_read中已经帮你处理了信号中断的错误
    }
    *bufp = 0;  // 使用NULL(0)结束文本行
    return n-1; // 实际读取的字节数目（不包括结尾的NULL）
}