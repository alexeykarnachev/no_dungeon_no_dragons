game:
	g++ \
	-std=c++17 \
	-I./deps/include \
	-o ./build/linux/game \
	./bin/game.cpp \
	-L./deps/lib/linux -lraylib -lGL -lpthread -ldl
