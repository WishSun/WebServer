/*************************************************************************
	> File Name: calc_cgi.cpp
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年08月14日 星期二 13时20分17秒
 ************************************************************************/

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
using namespace std;

/* 获取请求参数*/
bool get_arg(char *buff)
{
    char *method = NULL;
    char *content_length = NULL;

    /* 从环境变量中获取请求方法，因为已经由父进程设置，而子进程继承父进程的环境变量*/
    method = getenv("METHOD");
    if( method && (strcasecmp(method, "POST") == 0) )
    {
        content_length = getenv("CONTENT_LENGTH");
        if( ! content_length )
        {
            return false;
        }

        int length = atoi(content_length);
        int have_read = 0;
        int ret;
        while( have_read < length && (ret = read(STDIN_FILENO, buff + have_read, length)) > 0 )
        {
            have_read += ret;
        }
        return true;
    }

    return false;
}

void getSum(char *sum, char *op1, char *op2)
{
    sprintf(sum, "%s%s", op1, op2);
}

void send_200_OK(int content_length)
{
    printf("HTTP/1.1 200 OK\r\n");
    printf("Server: My Web Server\r\n");
    printf("Connection: close\r\n");
    printf("Content-length: %d\r\n", content_length);
    printf("Content-type: text/html; charset=UTF-8\r\n\r\n");
}

bool calc_arg(char *buff)
{
    char *op1 = NULL, *op2 = NULL;   
    char *pBuff = buff;
    int i = 0;

    while( '\0' != *pBuff )
    {
        if( *pBuff == '=' )
        {
            if( op1 == NULL )
            {
                op1 = pBuff+1;
            }
            else if( op2 == NULL )
            {
                op2 = pBuff+1;
            }
            else
            {
                return false;
            }
        }
        else if( *pBuff == '&' )
        {
            *pBuff = '\0';
        }

        pBuff++;
    }

    char sum[ 1024 ] = "";
    getSum(sum, op1, op2);
    char response[ BUFSIZ ] = "";
    sprintf(response, "<html><body> <h1> 计算 %s + %s 的结果？ </h1><h1> %s + %s = %s </h1></body><html>", op1, op2, op1, op2, sum );

    send_200_OK(strlen(response));
    printf("%s", response);

    return true;
}

int main(void)
{
    char buff[1024];

    get_arg(buff);
    calc_arg(buff);
    return 0;
}
