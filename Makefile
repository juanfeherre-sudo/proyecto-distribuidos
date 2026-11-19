# Compila los 5 programas:  make        Borra los binarios:  make clean
# Cada programa depende tambien de common.hpp: si cambia, se recompilan todos.
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -I.

all: cliente gestor rmc cancelaciones persistencia

cliente: cliente.cpp common.hpp
	$(CXX) $(CXXFLAGS) cliente.cpp -o cliente -lzmq -pthread

gestor: gestor.cpp common.hpp
	$(CXX) $(CXXFLAGS) gestor.cpp -o gestor -lzmq

rmc: rmc.cpp common.hpp
	$(CXX) $(CXXFLAGS) rmc.cpp -o rmc -lzmq

cancelaciones: cancelaciones.cpp common.hpp
	$(CXX) $(CXXFLAGS) cancelaciones.cpp -o cancelaciones -lzmq

persistencia: persistencia.cpp common.hpp
	$(CXX) $(CXXFLAGS) persistencia.cpp -o persistencia -lzmq -lsqlite3

clean:
	rm -f cliente gestor rmc cancelaciones persistencia

.PHONY: all clean
