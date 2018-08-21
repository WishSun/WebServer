target:
	#生成静态库
	make -C ./static/parse_cfg/
	#生成可执行程序
	make -C ./src/

install:
	#将可执行文件拷贝到/bin目录下
	cp ./src/server ./bin/
uninstall:
	#卸载
	rm -rf ./bin/server

.PHONY:clean
clean:
	make clean -C ./src/
	make clean -C ./static/parse_cfg/
