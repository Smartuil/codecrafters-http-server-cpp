#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

int main(int argc, char **argv)
{
    // 设置 std::cout 和 std::cerr 为无缓冲模式
    // 每次输出后立即刷新，便于调试时实时看到日志
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // 调试日志，运行测试时会显示
    std::cout << "Logs from your program will appear here!\n";

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

        // ==================== 第七步：读取并解析 HTTP 请求 ====================
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

        // 解析请求行，提取 URL 路径
        // 请求行格式: "METHOD /path HTTP/1.1"
        // 找到第一个空格后的位置就是路径开始
        // 找到第二个空格的位置就是路径结束
        std::string path;
        size_t method_end = request.find(' ');
        if (method_end != std::string::npos)
        {
            size_t path_end = request.find(' ', method_end + 1);
            if (path_end != std::string::npos)
            {
                path = request.substr(method_end + 1, path_end - method_end - 1);
            }
        }
        std::cout << "Extracted path: " << path << std::endl;

        // ==================== 第八步：发送 HTTP 响应 ====================
        // 根据请求路径返回不同的响应：
        // - 路径为 "/" 时返回 200 OK
        // - 其他路径返回 404 Not Found
        std::string response;
        if (path == "/")
        {
            response = "HTTP/1.1 200 OK\r\n\r\n";
        }
        else
        {
            response = "HTTP/1.1 404 Not Found\r\n\r\n";
        }
        send(client_fd, response.c_str(), response.size(), 0);

        // ==================== 第九步：关闭客户端套接字 ====================
        close(client_fd);
    }

    // 关闭服务器监听套接字，释放资源
    close(server_fd);

    return 0;
}
