/*************************************************************************
	> File Name: main.c
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年08月18日 星期六 04时26分35秒
 ************************************************************************/

#include<stdio.h>
#include <fcntl.h>
#include <unistd.h>


int main(void)
{
    int fd = open("./index.html", O_RDWR);
    lseek(fd, 1024*1024*10, SEEK_END);
    int a = 2;
    write(fd, &a, sizeof(a));
    close(fd);
    return 0;
}
