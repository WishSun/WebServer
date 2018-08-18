# 项目名称：WebServer
一个实现了get，post请求的Web服务器
## 文件划分
    bin:可执行文件目录
    etc:配置文件目录
    lib:静态库文件目录
    static:静态库源文件目录
    wwwRoot:web服务器根目录
    inc:头文件目录
    src:源文件目录
    文档:项目文档目录
## 使用方法
### **进入WebServer目录后，依次执行以下命令:**
    make clean   //清除
    make         //重新编译
    make install //将可执行文件安装到bin目录下
    cd bin       //进入可执行文件目录
    ./server     //执行服务器程序
## **注意: 如果不能运行，可能是服务器IP地址不对，需配置为自己机器的IP地址，通过修改etc目录下的web.cfg文件即可配置**
