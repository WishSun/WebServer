SRC=$(wildcard ./*.cpp)
OBJ=$(patsubst %.cpp, %.o, $(SRC))
BIN=./calc_cgi

$(BIN):$(OBJ)
	g++ $^ -o $@  
./%.o:./%.cpp
	g++ -c $< -o $@ -g 

.PHONY:clean
clean:
	rm -rf $(BIN) $(OBJ)
