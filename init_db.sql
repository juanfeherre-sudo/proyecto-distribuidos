CREATE TABLE Eventos (
	idEvento TEXT PRIMARY KEY,
	fecha TEXT,
	asientosLibres INTEGER,
	asientosOcupados INTEGER
);

CREATE TABLE Reservas (
	idReservas INTEGER PRIMARY KEY AUTOINCREMENT,
	idEvento TEXT,
	cliente TEXT,
	numAsientos INTEGER,
	FOREIGN KEY (idEvento) REFERENCES Eventos(idEvento)
);

INSERT INTO Eventos VALUES ('E1', '2026-10-15', 100, 0);
INSERT INTO Eventos VALUES ('E2', '2026-10-20', 100, 0);
INSERT INTO Eventos VALUES ('E2b', '2026-10-25', 100, 0);
