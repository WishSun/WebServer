/*************************************************************************
	> File Name: http_conn.cpp
	> Author: WishSun
	> Mail: WishSun_Cn@163.com
	> Created Time: 2018年04月15日 星期日 19时39分30秒
 ************************************************************************/
#include "../../inc/http_conn.h"
#include <string.h>
#include <sys/wait.h>

/* 定义一些HTTP响应的一些状态信息*/
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";
const char *wwwRoot = "../wwwRoot";

#define PATH_MAX 1024

/* 获取html和cgi所在目录*/
int get_root_path(char *root_path)
{
    char buff[ PATH_MAX ] = {0};

    /* 通过/proc/self/exe文件获取当前程序的运行绝对路径，并将路径放到 buff 中*/
    int ret = readlink("/proc/self/exe", buff, PATH_MAX);
    if (ret < 0)
    {
        return -1;
    }

    /* 刚才获取的绝对路径包含了程序名，而我们需要的是当前程序所在的目录，所以不需要程序名( 找到最后一个'/', 并将其后面的部分截断 )*/
    char *ptr = strrchr(buff, '/');
    if ( ptr )
    {
        *ptr = '\0';
    }

    /* 将配置文件路径组装好并填充到 root_path 中*/
    snprintf(root_path, PATH_MAX, "%s/%s", buff, wwwRoot);

    return 0;
}

/* 设置描述符fd为非阻塞*/
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}
/* 将描述符fd添加到内核事件监听表epollfd中
 * EPOLLRDHUP 事件: 我们期望一个socket连接在任一时刻都只被一个线程处理
 *            对于注册了EPOLLONESHOT事件的文件描述符，操作系统最多触发
 *            其上注册的一个可读、可写或者异常事件，且只触发一次，除非
 *            我们使用epoll_ctl函数重置还文件描述符上注册的EPOLLONESHOT
 *            事件
 *        实现流程:
 *                当接受到一个socket连接时，先设定其EPOLLONESHOT事件，等到
 *            其上边有数据可读或可写时，一个线程直接去处理，而在处理的
 *            过程中该socket描述符将不会再被触发;
 *                当线程将数据处理完之后，再次为该描述符设定EPOLLONESHOT
 *            事件，则它又可以被触发了
 */
void addfd( int epollfd, int fd, bool one_shot )
{
    epoll_event event;
    event.data.fd = fd;
    /* 检测可读事件，指定为边缘触发，而且指定EPOLLDHUP(TCP连接被对方关闭)*/
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;   
    if( one_shot )
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking( fd );
}

/* 将描述符fd从内核事件监听表中删除*/
void removefd(int epollfd, int fd)
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close(fd);
}

/* 重新设定fd的事件(主要是EPOLLONESHOT)*/
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

/* 初始化类静态变量，为类内函数提供定义----------------------------------------*/
/* 初始化用户数量，和epoll监听事件表描述符*/
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/* 客户方关闭了连接*/
void http_conn::close_conn( bool read_close )
{
    if( read_close && ( m_sockfd != -1 ) )
    {
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        m_user_count--;
    }
}

/* 初始化该HTTP连接*/
void http_conn::init( int sockfd, const sockaddr_in &addr )
{
    m_sockfd = sockfd;
    m_address = addr;
    /*如下两行是为了避免TIME_WAIT状态，仅用于调试，实际使用时应该去掉*/
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    addfd( m_epollfd, sockfd, true );
    m_user_count++;
    init();
}

/* 初始化调用*/
void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;    /* 设置行处理初始状态*/
    m_linger = false;
    m_method = GET;
    m_url = NULL;
    m_version = 0;
    m_content_length = 0;
    m_host = NULL;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_bytes_to_send = 0;
    m_bytes_have_send = 0;
    memset( m_read_buf, '\0', READ_BUFFER_SIZE );
    memset( m_write_buf, '\0', WRITE_BUFFER_SIZE );
    memset( m_read_file, '\0', FILENAME_LEN );
}
/* 从状态机*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    /* m_checked_idx 是当前正在分析的字符
     * m_read_idx 是指向读缓冲区m_read_buf中客户数据的尾部的下一字节
     * 即 : m_read_buf中第0 - m_checked_idx字节都已经分析完毕，第m_checked_idx - (m_read_idx - 1)字节还未分析
     */
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx )
    {
        /* 获得当前要分析的字节*/
        temp = m_read_buf[ m_checked_idx ];
        /*如果当前字节是 '\r', 即回车符，则说明可能读取到一个完整的行(还需要一个'\n')*/
        if( temp == '\r' )
        {
            /* 如果 '\r' 恰好是已读入的数据的最后一个字节，说明这次分析没有拿到一个完整的行, 返回LINE_OPEN
             * 以表示还需要继续读取客户端数据才能进一步分析
             */
            if( ( m_checked_idx + 1 ) == m_read_idx )
            {
                return LINE_OPEN;
            }
            /* 如果下一个字符刚好是 '\n', 说明我们成功读取到了一个完整的行, 则在行尾设置字符串结束标志并返
             * 回LINE_OK
             */
            else if( m_read_buf[ m_checked_idx + 1 ] == '\n')
            {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            /* 如果以上两种情况都不是，则说明在行的中间出现了 '\r', 而不是行尾，说明该请求存在语法问题*/
            return LINE_BAD;
        }
        /* 如果当前字节是 '\n', 即换行符，则也说明可能读取到一个完整的行(还需要前一个字符为'\r')*/
        else if( temp == '\n' )
        {
            /* 如果前一个字符恰好是 '\r', 则说明成功读取到了一个完整的行，则在行尾设置字符串结束标志并返
             * 回LINE_OK
             */
            if( ( m_checked_idx > 1 ) && ( m_read_buf[ m_checked_idx - 1 ] == '\r'))
            {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            /* 如果前一个字符不是 '\r', 则说明该请求存在语法问题*/
            return LINE_BAD;
        }
    }
    /* 如果当前已读入的数据已全部分析完，却没有碰见行尾，说明未读取到完整的行，所以返回LINE_OPEN,表示
     * 仍需继续读取客户端数据，以进一步分析
     */
    return LINE_OPEN;
}

/* 循环读取客户数据， 直到无数据可读或者对方关闭连接*/
bool http_conn::read_request()
{
    if( m_read_idx >= READ_BUFFER_SIZE )
    {
        return false;
    }
    int bytes_read = 0;
    while( true )
    {
        /* 由于m_sockfd是非阻塞的，所以本次调用不会阻塞*/
        bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        /* 如果调用返回-1，错误代码为  EAGAIN | EWOULDBLOCK,
         * 并不是因为数据出错,是因为在非阻塞模式下调用了阻塞操作，而操作未完成导致的。
         * 其他的错误代码说明是数据出错
         */
        if( bytes_read == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            return false;
        }
        /* 对方关闭了连接*/
        else if( bytes_read == 0 )
        {
            return false;
        }

        /* 更新已读入数据的下一字节m_read_idx位置*/
        m_read_idx += bytes_read;
    }
    return true;
}

/* 解析HTTP请求行，获得请求方法、目标URL，以及HTTP版本号*/
http_conn::HTTP_CODE http_conn::parse_request_line( char *text )
{
    /* 一个HTTP请求行栗子:
     *     GET http://www.baidu.com/index.html HTTP/1.0
     *     User-Agent: Wget/1.12 (linux-gnu)
     *     Host: www.baidu.com
     *     Connection: close
     */
    /* url 可见在请求的第一个空字符(空格、制表符)之后，strpbrk返回text中第一个空字符出现
     * 位置指针，那么该位置指针的下一个位置即为url的第一个字符
     */
    m_url = strpbrk( text, " \t" );
    /* 如果请求行中没有空格或制表符，则HTTP请求必有问题*/
    if ( ! m_url )
    {
        return BAD_REQUEST;
    }
    /* 将请求方法和URL分隔开*/
    *m_url++ = '\0';
    char *method = text;

    /* 仅支持GET和POST方法*/
    if ( strcasecmp( method, "GET" ) == 0 )
    {
        m_method = GET;
    }
    else if( strcasecmp( method, "POST" ) == 0 )
    {
        m_method = POST;
    }
    else
    {
        return BAD_REQUEST;
    }
    /* strspn返回字符串m_url中第一个不包含于字符串" \t"的字符的索引，
     * 即跳过URL字段之前的空字符，使得m_url直接指向URL的第一个字符
     */
    m_url += strspn( m_url, " \t" );
    /* URL的下一个字段是版本号version*/
    m_version = strpbrk( m_url, " \t" );
    if ( ! m_version )
    {
        return BAD_REQUEST;
    }
    /* 将URL和version分隔开*/
    *m_version++ = '\0';
    /* 使得m_version直接指向version的第一个字符*/
    m_version += strspn( m_version, " \t" );
    /* 仅支持HTTP/1.1版本*/
    if ( strcasecmp( m_version, "HTTP/1.1" ) != 0 )
    {
        return BAD_REQUEST;
    }
    /* 检查URL是否合法*/
    if ( strncasecmp( m_url, "http://", 7 ) == 0 )
    {
        m_url += 7;
        m_url = strchr( m_url, '/' );  /* 找到网站根目录字符'/'首次出现的位置*/
    }
    /* 如果URL中没有出现'/', 则说明其语法有问题*/
    if( ! m_url || m_url[ 0 ] != '/' )
    {
        return BAD_REQUEST;
    }
    /* 请求行已分析完毕，接下来就要开始分析头部字段，所以主状态机的状态迁移为CHECK_STATE_HEADER*/
    m_check_state = CHECK_STATE_HEADER;
    /* 返回NO_REQUEST, 表示请求还未分析完，因为头部字段还未分析*/
    return NO_REQUEST;
}

/* 解析HTTP请求的一个头部信息*/
http_conn::HTTP_CODE http_conn::parse_headers( char *text )
{
    /* 遇到空行，表示头部字段解析完毕*/
    if ( text[ 0 ] == '\0')
    {
        /* 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
         * 状态转移到CHECK_STATE_CONTENT状态
         */
        if ( m_content_length != 0 )
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        /* 头部已解析完毕，没有消息体，整个请求及其头部检验完毕，开始处理请求*/
        return GET_REQUEST;
    }
    /* 处理Connection头部字段*/
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 )
    {
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 )
        {
            m_linger = true;
        }
    }
    /* 处理Content-Length头部字段*/
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 )
    {
        text += 15;
        text + strspn( text, " \t" );
        m_content_length = atol( text ); /* 将字符串转为数字*/
    }
    /* 处理Host头部字段*/
    else if ( strncasecmp( text, "Host:", 5 ) == 0 )
    {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    }
    else
    {
        //printf("oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

/* 解析消息体，即解析post请求的参数*/
http_conn::HTTP_CODE http_conn::parse_content( char *text )
{
    /* 请求不完整，还有数据未获取完*/
    if( m_read_idx < ( m_content_length + m_checked_idx ) )
    {
        return NO_REQUEST;
    }

    int fa_To_ch[2];   /* 父进程往子进程送数据*/
    int ch_To_fa[2];   /* 子进程往父进程送数据*/
    if( pipe(fa_To_ch) < 0 )
    {
        perror("pipe:");
    }
    if( pipe(ch_To_fa) < 0 )
    {
        perror("pipe:");
    }

    int pid;
    if( (pid = fork()) < 0 )
    {
        perror("fork:");
    }

    if( pid == 0 )
    {
        char METHOD[BUFSIZ];
        sprintf(METHOD, "METHOD=%s", "POST");
        char CONTENT_LENGTH[BUFSIZ];
        sprintf(CONTENT_LENGTH, "CONTENT_LENGTH=%d", m_content_length);

        putenv(METHOD);
        putenv(CONTENT_LENGTH);


        /* 用cgi程序替换当前子进程*/
        char cgi_path[ PATH_MAX ] = "";
        get_root_path( cgi_path );
        strcat(cgi_path, "/cgi-bin/calc_cgi");

        /* 子进程从fa_To_ch[0], 现在已重定向到标准输入读取父进程发送给子进程的数据
         * 将处理好的数据，发送到ch_To_fa[1], 现在已重定向到标准输出。
         */
        close(fa_To_ch[1]);
        dup2(fa_To_ch[0], STDIN_FILENO);
        close(ch_To_fa[0]);
        dup2(ch_To_fa[1], STDOUT_FILENO);

        execl(cgi_path, cgi_path, NULL);
        exit(1);
    }
    /* 父进程*/
    else
    {
        close(fa_To_ch[0]);
        close(ch_To_fa[1]);
        

        int have_write = 0;
        int ret;
        while( (have_write < m_content_length) && (ret = write(fa_To_ch[1], text, m_content_length)) > 0 )
        {
            have_write += ret;
        }
        
        char buff[ BUFSIZ ] = "";
        while( (ret = read(ch_To_fa[0], buff, BUFSIZ)) > 0 )
        {
            write(m_sockfd, buff, ret);
        }
       
        waitpid(pid, NULL, 0);
        close(ch_To_fa[0]);
        close(fa_To_ch[1]);

        return GET_REQUEST;
    }
}

/* 主状态机*/
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = NULL;
    /* while循环条件: 1.首先查看主状态机状态是否为 CHECK_STATE_CONTENT, 即当前是否在分析消息体，
     *                以及是否接收到一个完整的行(包括请求行和头部字段)
     *                2. 如果条件 1 未达到的话，则进行分析是否收到一个完整的行，如果收到了
     *                即返回值为 LINE_OK，则开始解析这个完整的行。 
     *
     *                如果条件1和条件2都未达到，则退出while循环，因为还没有读到一个完整的
     *                行，则表示还需要继续读取客户数据才能进一步分析
     *
     */
    while ( ( ( m_check_state == CHECK_STATE_CONTENT )  && ( line_status == LINE_OK ))
    ||  ( ( line_status = parse_line()  ) == LINE_OK))
    {
        /* 将text设置当前正在分析的完整行的起始字符位置, 即得到当前行,
         * 当m_checked_idx指向请求的最后一个回车换行后的内容时，即指向'\0',说明请求内容分析完毕
         */
        text = get_line();

        /* 将当前正在解析的行的起始位置设置为已读入的一个完整行的下一个位置, 为下个循环准备*/
        m_start_line = m_checked_idx;
        switch ( m_check_state )
        {
            case CHECK_STATE_REQUESTLINE:          /* 第一个状态: 分析请求行*/
            {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:              /* 第二个状态: 分析头部字段*/
            {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                else if ( ret == GET_REQUEST && m_method == GET )
                {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:            /* 第三个状态: 分析消息体*/
            {
                return parse_content( text );
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    /* 退出while循环，而不是直接返回，说明尚未读取到一个完整的行*/
    return NO_REQUEST;
}

/*  当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，如果目标文件存在
 *  对所有用户可读，且不是目录，则使用mmap将其映射到内存地址m_file_address处，并
 *  告诉调用者获取文件成功
 */
http_conn::HTTP_CODE http_conn::do_request()
{
    /* 获取文件在服务器的真实路径( html_path + m_url )*/
    char html_path[ PATH_MAX ] = {0};
    get_root_path(html_path);
    strcpy( m_read_file, html_path );
    int len = strlen( html_path );
    if( strcasecmp(m_url, "/") == 0 )
    {
        strncpy( m_read_file + len, "/index.html", FILENAME_LEN - len - 1 );
    }
    else
    {
        strncpy( m_read_file + len, m_url, FILENAME_LEN - len - 1 );
    }

    /* 获取文件的属性*/
    if ( stat( m_read_file, &m_file_stat ) < 0 )
    {
        return NO_RESOURCE;    /* 文件不存在*/
    }

    /* 查看权限是否满足( 其他人(others)是否有读权限 )*/
    if ( ! ( m_file_stat.st_mode & S_IROTH ) )
    {
        return FORBIDDEN_REQUEST;  /* 权限不足*/
    }

    /* 文件类型是否为目录*/
    if ( S_ISDIR( m_file_stat.st_mode ) )
    {
        return BAD_REQUEST;
    }

    /* 以只读打开文件，并映射到内存*/
    int fd = open( m_read_file, O_RDONLY );

    /* 参数1: 0，表示使用系统自动分配的地址 
     * 参数2: 映射内存的大小
     * 参数3: PROT_READ，映射内存的权限为只读
     * 参数4: MAP_PRIVATE，内存段为调用进程所私有。对该内存段的修改不会反映到被映射的文件中
     * 参数5: 被映射文件打开的文件描述符
     * 参数6: 映射文件的内部偏移值，0表示从文件起始处开始
     */
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ,
                                  MAP_PRIVATE, fd, 0);

    /* 映射完毕后，关闭文件描述符*/
    close( fd );

    return FILE_REQUEST;
}

/* 对内存映射区执行munmap操作*/
void http_conn::unmap()
{
    if ( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = NULL;
    }
}

/* 写HTTP响应*/
bool http_conn::write_response()
{
    int temp = 0;

    /* 如果没有要发送的数据了，那么可以再去获取客户端请求了*/
    if ( m_bytes_to_send == 0 )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    /* 一定保证将响应头在这成功发送*/
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    while( bytes_have_send < bytes_to_send )
    {
        temp = write(m_sockfd, m_write_buf, m_write_idx);
        if( temp == -1 )
        {
            if ( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                continue;
            }
        }
        bytes_have_send += temp;
    }
    m_write_idx = 0;


    /* 发送响应文件体, 可能本次发送不完，需要等待EPOLLOUT事件*/
    while( 1 )
    {
        temp = write(m_sockfd, m_file_address + m_bytes_have_send, m_file_stat.st_size);
        if ( temp <= -1 )
        {
            /* 如果TCP写缓冲没有空间，则等待下一轮 EPOLLOUT 事件。
             * 虽然在此期间，服务器无法立即接收到同一客户的下一个
             * 请求,但这可以保证连接的完整性
             */
            if ( errno == EAGAIN )
            {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        /* 更新 m_bytes_to_send 和 m_bytes_have_send 的值*/
        m_bytes_to_send -= temp;
        m_bytes_have_send += temp;

        /* 如果写完了, 就去监听EPOLLIN，不再监听EPOLLOUT事件了*/
        if ( m_bytes_to_send <= m_bytes_have_send )
        {
            /* 将记录置空*/
            m_bytes_have_send = 0;
            m_bytes_to_send = 0;

            /* 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否
             * 立即关闭连接
             */
            unmap();
            if( m_linger )
            {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            }
            else
            {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            }
        }
    }
}

/* 往写缓冲中写入待发送的数据*/
bool http_conn::add_response( const char *format, ... )
{
    if( m_write_idx >= WRITE_BUFFER_SIZE )
    {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    /* 将最多size字节的数据写到str中
     * int vsnprintf(char *str, 
     *               size_t size,
     *               const char *format,
     *               va_list ap);
     */
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,
                         format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) )
    {
        return false;
    }
    /* 更新m_write_idx*/
    m_write_idx += len;
    va_end( arg_list );
    return true;
}
/* 写消息体*/
bool http_conn::add_content( const char *content )
{
    return add_response(content);
}
/* 将对HTTP请求的响应状态写入写缓冲: 例如: HTTP/1.1 200 OK*/
bool http_conn::add_status_line( int status, const char *title )
{
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}
/* 将HTTP响应的头部字段写入写缓冲*/
bool http_conn::add_headers( int content_len )
{
    add_content_length( content_len );
    add_linger();
    add_blank_line();
}
/* 向写缓冲中写入消息体长度*/
bool http_conn::add_content_length( int content_len )
{
    return add_response( "Content-Length: %d\r\n", content_len );
}
/* 向写缓冲中写入连接信息*/
bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}
/* 向写缓冲中写入空行*/
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}
/* 根据服务器处理HTTP请求的结果，决定返回给客户端的内容并写入写缓冲*/
bool http_conn::process_write( HTTP_CODE ret )
{
    switch ( ret )
    {
        /* 服务器内部错误*/
        case INTERNAL_ERROR:
        {
            add_status_line( 500, error_500_title );    /*错误标题*/
            add_headers( strlen( error_500_form ) );    /*消息体长度*/ 
            if ( ! add_content( error_500_form ) )      /*消息体为错误描述信息*/
            {
                return false;
            }
            break;
        }
        /* 错误的请求语法*/
        case BAD_REQUEST:
        {
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) )
            {
                return false;
            }
            break;
        }
        /* 请求的资源不存在*/
        case NO_RESOURCE:
        {
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) )
            {
                return false;
            }
            break;
        }
        /* 请求的资源服务器没有访问权限*/
        case FORBIDDEN_REQUEST:
        {
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if ( ! add_content( error_403_form ) )
            {
                return false;
            }
            break;
        }
        /* 正确的文件请求*/
        case FILE_REQUEST:
        {
            add_status_line( 200, ok_200_title );
            if ( m_file_stat.st_size != 0 )
            {
                add_headers( m_file_stat.st_size );
                m_bytes_to_send = m_file_stat.st_size;
               
                return true;
            }
            /* 如果文件为空文件的话，则构造一个空html网页*/
            else
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers( strlen( ok_string ) );
                if ( ! add_content( ok_string ) )
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;       
        }
    }
    return true;
}

/* 由线程池中的工作线程调用，这是处理HTTP请求的入口函数*/
void http_conn::process()
{
    /* 进入主状态机，处理客户请求*/
    HTTP_CODE read_ret = process_read();

    /* 如果请求不完整，则将该客户端连接再次放入事件监听表，读取其后续数据*/
    if ( read_ret == NO_REQUEST )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }
    /* 根据服务器对客户端请求的结果，向写缓冲写入对客户端回复响应*/
    bool write_ret = process_write( read_ret );
    if ( ! write_ret )
    {
        close_conn();
    }
    /* 监听可写事件，监听到可写时，将写缓冲中的响应发送给客户端*/
    modfd( m_epollfd, m_sockfd, EPOLLOUT );
}
