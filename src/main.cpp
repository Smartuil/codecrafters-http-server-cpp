#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>    // 用于多线程支持
#include <fstream>   // 用于文件操作
#include <sstream>   // 用于字符串流

// 全局变量：存储文件目录路径
std::string g_directory;

// 处理单个客户端连接的函数
// 将其抽取为独立函数，以便在新线程中执行
void handle_client(int client_fd)
{
    // ==================== 读取并解析 HTTP 请求 ====================
    // HTTP 请求由三部分组成，每部分以 CRLF (\r\n) 分隔：
    // 1. 请求行 (Request line): 方法 + 请求目标(URL路径) + HTTP版本
    // 2. 请求头 (Headers): 零个或多个，每个以 CRLF 结尾
    // 3. 请求体 (Body): 可选的请求内容
    //
    // 示例: "GET /index.html HTTP/1.1\r\nHost: localhost:4221\r\n\r\n"
    char buffer[1024] = {0};
    recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    std::string request(buffer);
    std::cout << "Received request:\n" << request << std::endl;

    // 解析请求行，提取 HTTP 方法和 URL 路径
    // 请求行格式: "METHOD /path HTTP/1.1"
    // 找到第一个空格后的位置就是路径开始
    // 找到第二个空格的位置就是路径结束
    std::string method;
    std::string path;
    size_t method_end = request.find(' ');
    if (method_end != std::string::npos)
    {
        method = request.substr(0, method_end);
        size_t path_end = request.find(' ', method_end + 1);
        if (path_end != std::string::npos)
        {
            path = request.substr(method_end + 1, path_end - method_end - 1);
        }
    }
    std::cout << "Method: " << method << ", Path: " << path << std::endl;

    // ==================== 发送 HTTP 响应 ====================
    // 根据请求路径返回不同的响应：
    // - 路径为 "/" 时返回 200 OK
    // - 路径以 "/echo/" 开头时返回 200 OK，响应体为路径中的字符串
    // - 路径为 "/user-agent" 时返回 200 OK，响应体为 User-Agent 头的值
    // - 其他路径返回 404 Not Found
    std::string response;
    if (path == "/")
    {
        response = "HTTP/1.1 200 OK\r\n\r\n";
    }
    else if (path.substr(0, 6) == "/echo/")
    {
        // 提取 /echo/ 后面的字符串作为响应体
        // 例如: /echo/abc -> abc
        std::string echo_str = path.substr(6);
        // 构建响应:
        // - Content-Type: text/plain 表示响应体是纯文本
        // - Content-Length: 响应体的字节长度
        response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Length: " + std::to_string(echo_str.size()) + "\r\n";
        response += "\r\n";  // 头部结束
        response += echo_str;  // 响应体
    }
    else if (path == "/user-agent")
    {
        // 从请求头中提取 User-Agent 的值
        // 请求头格式: "User-Agent: value\r\n"
        // 注意: 头名称不区分大小写
        std::string user_agent;
        std::string ua_header = "User-Agent: ";
        size_t ua_pos = request.find(ua_header);
        if (ua_pos != std::string::npos)
        {
            size_t value_start = ua_pos + ua_header.size();
            size_t value_end = request.find("\r\n", value_start);
            if (value_end != std::string::npos)
            {
                user_agent = request.substr(value_start, value_end - value_start);
            }
        }
        // 构建响应
        response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/plain\r\n";
        response += "Content-Length: " + std::to_string(user_agent.size()) + "\r\n";
        response += "\r\n";
        response += user_agent;
    }
    else if (path.substr(0, 7) == "/files/")
    {
        // 处理文件请求: /files/{filename}
        // 从路径中提取文件名
        std::string filename = path.substr(7);
        // 构建完整的文件路径
        std::string filepath = g_directory + filename;
        
        if (method == "GET")
        {
            // GET 请求：读取文件并返回内容
            std::ifstream file(filepath, std::ios::binary);
            if (file.is_open())
            {
                // 文件存在，读取文件内容
                std::stringstream file_buffer;
                file_buffer << file.rdbuf();
                std::string file_content = file_buffer.str();
                file.close();
                
                // 构建 200 响应
                // Content-Type: application/octet-stream 表示二进制文件
                response = "HTTP/1.1 200 OK\r\n";
                response += "Content-Type: application/octet-stream\r\n";
                response += "Content-Length: " + std::to_string(file_content.size()) + "\r\n";
                response += "\r\n";
                response += file_content;
            }
            else
            {
                // 文件不存在，返回 404
                response = "HTTP/1.1 404 Not Found\r\n\r\n";
            }
        }
        else if (method == "POST")
        {
            // POST 请求：将请求体内容写入文件
            // 从请求头中提取 Content-Length
            std::string content_length_header = "Content-Length: ";
            size_t cl_pos = request.find(content_length_header);
            int content_length = 0;
            if (cl_pos != std::string::npos)
            {
                size_t value_start = cl_pos + content_length_header.size();
                size_t value_end = request.find("\r\n", value_start);
                if (value_end != std::string::npos)
                {
                    content_length = std::stoi(request.substr(value_start, value_end - value_start));
                }
            }
            
            // 提取请求体（在 \r\n\r\n 之后的内容）
            std::string body;
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos)
            {
                body = request.substr(body_start + 4, content_length);
            }
            
            // 将请求体写入文件
            std::ofstream file(filepath, std::ios::binary);
            if (file.is_open())
            {
                file << body;
                file.close();
                // 返回 201 Created 响应
                response = "HTTP/1.1 201 Created\r\n\r\n";
            }
            else
            {
                // 无法创建文件，返回 500 错误
                response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
            }
        }
        else
        {
            response = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        }
    }
    else
    {
        response = "HTTP/1.1 404 Not Found\r\n\r\n";
    }
    send(client_fd, response.c_str(), response.size(), 0);

    // 关闭客户端套接字
    close(client_fd);
}

int main(int argc, char **argv)
{
    // 设置 std::cout 和 std::cerr 为无缓冲模式
    // 每次输出后立即刷新，便于调试时实时看到日志
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // 解析命令行参数
    // 支持 --directory 参数指定文件存储目录
    // 例如: ./your_program.sh --directory /tmp/
    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == "--directory" && i + 1 < argc)
        {
            g_directory = argv[i + 1];
            // 确保目录路径以 / 结尾
            if (!g_directory.empty() && g_directory.back() != '/')
            {
                g_directory += '/';
            }
            break;
        }
    }

    // 调试日志，运行测试时会显示
    std::cout << "Logs from your program will appear here!\n";
    std::cout << "Directory: " << g_directory << std::endl;

    // ==================== 第一步：创建服务器套接字 ====================
    // socket() 函数创建一个套接字
    // AF_INET: 使用 IPv4 协议
    // SOCK_STREAM: 使用 TCP 协议（面向连接的可靠传输）
    // 0: 自动选择协议（对于 SOCK_STREAM 默认是 TCP）
    // 返回值: 套接字文件描述符，失败返回 -1
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    // ==================== 第二步：设置套接字选项 ====================
    // 由于测试程序会频繁重启服务器，设置 SO_REUSEADDR 选项
    // 允许重用处于 TIME_WAIT 状态的地址，避免 "Address already in use" 错误
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    // ==================== 第三步：配置服务器地址结构 ====================
    struct sockaddr_in server_addr;
    // sin_family: 地址族，使用 IPv4
    server_addr.sin_family = AF_INET;
    // sin_addr.s_addr: 服务器 IP 地址
    // INADDR_ANY 表示监听所有可用的网络接口（0.0.0.0）
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // sin_port: 服务器端口号
    // htons() 将主机字节序转换为网络字节序（大端序）
    server_addr.sin_port = htons(4221);

    // ==================== 第四步：绑定套接字到地址 ====================
    // bind() 将套接字与指定的 IP 地址和端口绑定
    // 绑定成功返回 0，失败返回 -1
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }

    // ==================== 第五步：开始监听连接 ====================
    // listen() 将套接字设置为被动监听模式，准备接受客户端连接
    // connection_backlog: 等待连接队列的最大长度
    // 当队列满时，新的连接请求会被拒绝
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0)
    {
        std::cerr << "listen failed\n";
        return 1;
    }

    // ==================== 第六步：接受客户端连接 ====================
    // 用于存储客户端地址信息的结构体
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    std::cout << "Waiting for a client to connect...\n";

    // 使用循环处理多个客户端连接
    // 通过多线程实现并发处理，每个客户端连接在独立线程中处理
    while (true)
    {
        // accept() 阻塞等待客户端连接
        // 当有客户端连接时，返回一个新的套接字文件描述符用于与该客户端通信
        // 原来的 server_fd 继续用于监听新的连接
        // client_addr: 用于存储连接的客户端地址信息
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
        if (client_fd < 0)
        {
            std::cerr << "accept failed\n";
            continue;
        }
        std::cout << "Client connected\n";

        // 创建新线程处理客户端请求
        // 使用 detach() 让线程独立运行，主线程继续接受新连接
        // 这样可以同时处理多个并发连接
        std::thread client_thread(handle_client, client_fd);
        client_thread.detach();
    }

    // 关闭服务器监听套接字，释放资源
    close(server_fd);

    return 0;
}
