# Compila los 5 programas:  make        Borra los binarios:  make clean
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -I.

all: cliente gestor rmc cancelaciones persistencia

cliente: cliente.cpp
	$(CXX) $(CXXFLAGS) cliente.cpp -o cliente -lzmq -pthread

gestor: gestor.cpp
	$(CXX) $(CXXFLAGS) gestor.cpp -o gestor -lzmq

rmc: rmc.cpp
	$(CXX) $(CXXFLAGS) rmc.cpp -o rmc -lzmq

cancelaciones: cancelaciones.cpp
	$(CXX) $(CXXFLAGS) cancelaciones.cpp -o cancelaciones -lzmq

persistencia: persistencia.cpp
	$(CXX) $(CXXFLAGS) persistencia.cpp -o persistencia -lzmq -lsqlite3

clean:
	rm -f cliente gestor rmc cancelaciones persistencia

.PHONY: all clean
