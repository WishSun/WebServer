/*************************************************************************
	> File Name: http_server.cpp
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年07月06日 星期五 10时13分01秒
 ************************************************************************/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "../inc/locker.h"
#include "../inc/threadpool.h"
#include "../inc/http_conn.h"
#include "../inc/parse_configure_file.h"


#define MAX_FD 65536

/* 最大监听事件数*/
#define MAX_EVENT_NUMBER 10000

/* 最大路径长度*/
#define PATH_MAX 1024

/* 程序配置文件路径*/
#define CONF_PATH  "../etc/web.cfg"

char conf_path[ PATH_MAX ] = {0};

extern int addfd( int epollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd );

/* 注册信号及其信号处理函数*/
void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;

    /* restart 标志是: 重新调用被该信号终止的系统调用*/
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }

    /* 该信号的信号处理函数执行期间屏蔽一切信号*/
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

/* 输出错误信息, 并将该信息发送给服务器，然后关闭连接*/
void show_error( int connfd, const char *info )
{
    printf( "%s", info );
    write( connfd, info, strlen( info ) );
    close( connfd );
}

/* 获取当前运行程序的所在路径, 是为了得到配置文件的绝对路径*/
static int get_path()
{
    int ret = -1;
    char buff[ PATH_MAX ] = {0};
    char *ptr = NULL;

    /* 通过/proc/self/exe文件获取当前程序的运行绝对路径，并将路径放到 buff 中*/
    ret = readlink("/proc/self/exe", buff, PATH_MAX);
    if (ret < 0)
    {
        return -1;
    }

    /* 刚才获取的绝对路径包含了程序名，而我们需要的是当前程序所在的目录，所以不需要程序名( 找到最后一个'/', 并将其后面的部分截断 )*/
    ptr = strrchr(buff, '/');
    if ( ptr )
    {
        *ptr = '\0';
    }

    /* 将配置文件路径组装好并填充到 conf_path 中*/
    snprintf(conf_path, PATH_MAX, "%s/%s", buff, CONF_PATH);

    return 0;
}

int get_ip_port(char *conf_path, char *ip, int *port)
{
    if( conf_path == NULL )
    {
        printf("init_data arguments error!\n");
        return -1;
    }

    /* 打开配置文件*/
    if( open_conf( conf_path ) < 0 )
    {
        return -1;
    }

    const char *temp = "web_server_info.ip";
    /* 获取ip*/
    if( get_val_single( temp, ip, TYPE_STRING ) < 0 )
    {
        return -1;
    }
    /* 获取端口号*/
    const char *temp2 = "web_server_info.port";
    if( get_val_single( temp2, port, TYPE_INT ) < 0 )
    {
        return -1;
    }

    /* 关闭配置文件并释放资源*/
    close_conf();
    return 0;
}

int main(int argc, char* argv[])
{
    /* 从配置文件中获取ip地址和端口*/
    char ip[33] = {0};
    int port;
    get_path();

    if( get_ip_port(conf_path, ip, &port) < 0 )
    {
        printf(" get ip and port error!\n");
        return -1;
    }

    printf("ip: %s\nport: %d\n", ip, port);

    /* 忽略SIGPIPE信号*/
    addsig( SIGPIPE, SIG_IGN );

    /* 创建线程池*/
    threadpool< http_conn > *pool = NULL;
    try
    {
        pool = new threadpool< http_conn >;
    }
    catch( ... )
    {
        return 1;
    }

    /* 预先为每个可能的客户连接分配一个http_conn对象*/
    http_conn *users = new http_conn[ MAX_FD ];
    assert( users );
    int user_count = 0;

    /* 创建监听套接字*/
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    struct linger tmp = {1, 0};
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );
    
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    /* 绑定监听套接字到指定地址和端口*/
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    /* 创建epoll监听集合，并将监听套接字加入该集合*/
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( MAX_EVENT_NUMBER );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    
    while( true )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ))
        {
            printf( "epoll failure\n" );
            break;
        }

        for( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;

            /* 有新连接到来*/
            if( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address,
                                     &client_addrlength);
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    perror("accept:");
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD )
                {
                    show_error( connfd, "Internal server busy" );
                    continue;
                }

                /* 初始化客户连接*/
                users[ connfd ].init( connfd, client_address );
            }

            /* 如果有异常发生, 直接关闭连接*/
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                users[sockfd].close_conn();
            }

            /* 客户端有数据到来*/
            else if( events[i].events & EPOLLIN )
            {
                /* 根据读的结果，决定是将任务添加到线程池，还是关闭连接*/
                if( users[ sockfd ].read_request() )
                {
                    pool->append( users + sockfd );
                }
                else
                {
                    users[ sockfd ].close_conn();
                }
            }

            else if( events[ i ].events & EPOLLOUT )
            {
                /* 客户端连接已经可写，这时，将对客户端的响应写到客户端连接中*/
                if( !users[ sockfd ].write_response() )
                {
                    /* 根据写的结果，决定是否关闭连接*/
                    users[ sockfd ].close_conn();
                }
            }

            else
            { }
        }
    }

    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}
