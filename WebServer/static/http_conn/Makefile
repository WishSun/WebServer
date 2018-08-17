SRC=$(wildcard ./*.cpp)
OBJ=$(patsubst %.cpp, %.o, $(SRC))
LIB=../../lib/libhttp_conn.a

$(LIB):$(OBJ)
	ar -cr $@ $^

./%.o:./%.cpp
	gcc -c $< -o $@

.PHONY:clean
clean:
	rm $(OBJ) $(LIB)
