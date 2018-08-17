SRC=$(wildcard ./*.cpp)
OBJ=$(patsubst %.cpp, %.o, $(SRC))
BIN=./server

$(BIN):$(OBJ)
	g++ $^ -o $@  -L../lib -lpthread  -lhttp_conn  -lparse_configure_file  -lconfig
./%.o:./%.cpp
	g++ -c $< -o $@ -g 

.PHONY:clean
clean:
	rm -rf $(BIN) $(OBJ)
