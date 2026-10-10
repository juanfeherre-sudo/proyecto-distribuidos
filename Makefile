# Compila los 5 programas:  make        Borra los binarios:  make clean
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -I.

all: cliente gestor rmc cancelaciones persistencia

cliente: cliente.cpp config.h
	$(CXX) $(CXXFLAGS) cliente.cpp -o cliente -lzmq -pthread

gestor: gestor.cpp config.h
	$(CXX) $(CXXFLAGS) gestor.cpp -o gestor -lzmq

rmc: rmc.cpp config.h
	$(CXX) $(CXXFLAGS) rmc.cpp -o rmc -lzmq

cancelaciones: cancelaciones.cpp config.h
	$(CXX) $(CXXFLAGS) cancelaciones.cpp -o cancelaciones -lzmq

persistencia: persistencia.cpp config.h
	$(CXX) $(CXXFLAGS) persistencia.cpp -o persistencia -lzmq -lsqlite3

clean:
	rm -f cliente gestor rmc cancelaciones persistencia

.PHONY: all clean
