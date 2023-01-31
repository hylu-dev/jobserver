# Job Server

_This is a final assignment for CSC209 Systems Programming at the University of Toronto Mississauga._

The goal of this project is create a server capable of hosting multiple connections with each client able to send and receive information. In particular, clients can run basic C scripts on the server and have other clients choose to subscribe to the script status and output.

---

## Sheridan PGDAP Reviewer Considerations

### Involvement

This is an solo academic assignment completed by me. Some starter code including header files and empty functions were supplied by the professor but most of the code is written after by me. This project is included to demonstrate proficiency in C programming as well as experience in socket programming.

### Code Highlights
The project directory is small and made up of only a few scripts. The following scripts are written by me unless otherwise stated. The exception are the header files which are given as starter code.

- **jobserver.c**
  - Using the below scripts, starts the server and constantly listens for connections, jobs requests, and updates clients on jobs they are watching
  - Some signaling code and main function code are given as starter. Inline comments will indicate this.
- **jobprotocol.c**
  - Protocol for managing jobs that are added to the server
  - Memory is allocated for every job and freed when that job is finished or removed
  - Similar memory management is done for handling which clients are watching what jobs
  - **jobprotocol.h** is an assignment starter file given as a skeleton for jobprotocol.c
- **socket.c**
  - Sets up a server socket to accept connections from other clients  
  - **socket.h** is an assignment starter file given as a skeleton for socket.c


