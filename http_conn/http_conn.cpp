#include "http_conn.h"
// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

const char* doc_root = "resources";

int http_conn::pipefd[2];
#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000
const int TIMESHOT = 5;

void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL,new_flag);
}
void addfd(int epollfd,int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd= fd;
    //event.events = EPOLLIN | EPOLLRDHUP;
    event.events = EPOLLIN |  EPOLLRDHUP;

    if(one_shot){
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd ,&event);
    setnonblocking(fd);
}
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符
void modfd(int epoll_fd, int fd,int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events =ev  | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epoll_fd,EPOLL_CTL_MOD,fd,&event);

}


http_conn::http_conn(int port):
	_port(port),_timerList(new Timer_List())
      {
}
http_conn::~http_conn() {
    _timerMap.clear();
    delete _timerList;
    close(pipefd[0]);
    close(pipefd[1]);
}
int epollfd=-1;
int http_conn::m_epollfd= -1;
int http_conn::m_user_count =0 ;
int http_conn::start(){
    try{
        pool = new threadpool<http_conn>;
    }catch(...){
        return 1;
    }
    http_conn * users = new http_conn[ MAX_FD];
    int listenfd = socket(PF_INET, SOCK_STREAM,0);
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( _port );
    int reuse = 1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 5 );
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);

    addfd(epollfd,listenfd, false);
    http_conn::m_epollfd = epollfd;
    addSig(SIGALRM);
    alarm(TIMESHOT);
    while(1){
        int num = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if((num<0) && (errno !=EINTR)) {
            printf("epoll failure\n");
            break;

        }

        for(int i=0;i<num;i++){
            int sockfd =events[i].data.fd;
            uint32_t event = events[i].events;
            if(sockfd == listenfd) {
                int connfd = -1;
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                bzero(&client_address,sizeof(client_address));
                connfd = accept(listenfd,(struct sockaddr*)&client_address,&client_addrlen);
                
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if(http_conn::m_user_count >= MAX_FD) {
                    close(connfd);
                    continue;
                }
                setnonblocking(connfd);
                setTimer(connfd);
                users[connfd].init(connfd,client_address);
            }else if((sockfd == pipefd[0]) && (event & EPOLLIN)) {
                    //信号事件
                    handleSig(timeout);
                    //处理完，如果有超时事件，timeout为true
					if(timeout) {
					}
                } 
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                // users[sockfd].close_conn();
            } else if(events[i].events & EPOLLIN){
                if(users[sockfd].read(sockfd)){
                    pool->append(users + sockfd);
                }else{
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT) {
                if ( !users[sockfd].write())
                {
                    users[sockfd].close_conn();
                }
            }else {}

            if(timeout) {
                    handleTimer();
                    timeout = false;
                }
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;

}

void http_conn::init(int sockfd,const sockaddr_in & addr) {
    m_sockfd = sockfd;
    m_address = addr;

    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd,sockfd ,true);
    m_user_count++;
    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始状态为检查请求行
    m_linger = false;       // 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;         // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    bzero(m_read_buf, READ_BUF_SIZE);
    bzero(m_write_buf, READ_BUF_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}
void http_conn::close_conn(){
    if(m_sockfd != -1){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_conn::setTimer(int socket) {
    //创建定时器，绑定超时时间，添加到链表中
	auto it = _timerMap.find(socket);
	if(it != _timerMap.end()) {
		return false;
	}
    time_t tShot = time(NULL) + 3 * TIMESHOT;
    auto func = std::bind(&http_conn::disconnect,this,socket);
    Timer *timer = new Timer(socket, func, tShot);
    _timerMap[socket] = timer;
    _timerList->add_timer(timer);
	return true;	
}

//循环读取客户数据
bool http_conn::read(int fd){
    int cliSock = fd;
    if(m_read_idx >= READ_BUF_SIZE){
        return false;
    }

    //读取到的字节
    Timer *timer = nullptr;
	if(_timerMap.find(cliSock) != _timerMap.end()) {
		timer = _timerMap[cliSock];
	}
    int byte_read = 0;
    while (true)
    {
        /* code */
        byte_read = recv(m_sockfd, m_read_buf+m_read_idx,READ_BUF_SIZE - m_read_idx,0);
        if(byte_read == -1) {
            if( errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        else if(byte_read == 0){
            if(timer) _timerList->del_timer(timer);
            return false;
        }
        m_read_idx += byte_read;
        if(timer) {
            time_t cur = time(NULL);
            timer->expire = cur + 3 * TIMESHOT;
            _timerList->adjust_timer(timer);
        }
    }
    
    printf("读取到了数据：%s\n",m_read_buf);
    return true;
}
// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}
// 写HTTP响应
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;    // 已经发送的字节
    int bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        init();
        return true;
    }

    while(1) {
        // 分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send ) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if(m_linger) {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            } else {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUF_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUF_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUF_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );

    LOG_INFO("request:%s", m_write_buf);
    
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    return add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK)) {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_idx;
        //LOG_INFO("%s", text);

        switch ( m_check_state ) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content( text );
                if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            return true;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }
    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
    }
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
    }
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx ) {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' ) {
            if ( ( m_checked_idx + 1 ) == m_read_idx ) {
                return LINE_OPEN;
            } else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if( ( m_checked_idx > 1) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) ) {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}
// 由线程池中的工作线程调用
void http_conn::process(){
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }
    
    // 生成响应
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {
        close_conn();
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT);   
}


void http_conn::handleSig(bool &timeout) {
    //得到是否有超时事件，返回在timeout里
	if(timeout) {
		timeout = false;
		return ;
	}
    char signals[1024];
    int ret = recv(pipefd[0],signals,sizeof(signals),0);
    if(ret == -1) {
        std::cout<<"sig error"<<std::endl;
    }else if(ret == 0) return;
    else {
        for(int i = 0; i < ret; ++i) {
            if(signals[i] == SIGALRM) {
                timeout = true;
                break;
            }
        }
    }
}



void sendSig(int sig) {

    int save_errno = errno;
    int msg = sig;
    send(http_conn::pipefd[1],(char*)&msg,1,0);
    errno = save_errno;
}
void http_conn::addSig(int sig) {
    struct sigaction sa;
    sa.sa_handler = sendSig;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert( sigaction(sig,&sa,NULL) != -1 );
}

void http_conn::handleTimer() {
	if(timeout == false) {
		return ;
	}
    _timerList->tick();
	timeout = false;
    alarm(TIMESHOT);
}

void http_conn::disconnect(int cliScok) {
    removefd(epollfd,cliScok);
    close(cliScok);
    LOG_INFO("disconnect succeed");  
}