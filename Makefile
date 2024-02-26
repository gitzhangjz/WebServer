#wildcard – 查找指定目录下的指定类型的文件
SRC = $(wildcard src/*.cpp)
#patsubst – 匹配替换，体会这个模式替换，把相同的部分用%代替
OBJS = $(patsubst src/%.cpp, obj/%.o, $(SRC))
TARGET = a

#这些变量不能单独使用必须在命令中使用
#$@表示目标
#$^表示所有的依赖
#$<表示第一个依赖

$(TARGET) : $(OBJS)
	g++ $^ -o $@ -g -lmysqlclient -lpthread   

obj/%.o : src/%.cpp
	g++  $< -c -g -Iheader -o $@
#伪目标声明，不会该判断目标是否存在或者该目标是否需要更新
.PHONY:clean
clean : 
	rm -rf $(TARGET) obj/*
