SRC=$(wildcard ./*.cpp)
OBJ=$(patsubst %.cpp, %.o, $(SRC))
LIB=../../lib/libparse_configure_file.a

$(LIB):$(OBJ)
	ar -cr $@ $^

./%.o:./%.cpp
	gcc -c $< -o $@

.PHONY:clean
clean:
	rm $(OBJ) $(LIB)
