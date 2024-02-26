game:
	g++ \
	-I./deps/include \
	-o ./build/linux/game \
	./bin/game.cpp \
	./src/camera.cpp \
	./src/field.cpp \
	./src/renderer.cpp \
	./src/world.cpp \
	-L./deps/lib/linux -lraylib -lGL -lpthread -ldl
