# Tic-Tac-Toe
A network server that allows people to play Tic Tac Toe and chat with each other.

# How to Run the Program

This program is a server which accepts connections from an unlimited number of clients. 
To run *just* the server, type the following on the command line:

> gcc -Wall ticsvr.c

> ./a.out -p 1235

Note: the server runs on port 3000 by default. You can change the port number (anywhere from 1025 to 65535 with the -p flag)

You'll notice that nothing exciting happens--that's because the server is waiting for clients to connect. In a seperate tab,
you can type the following into the command line to connect as a client:

> nc teach.cs.toronto.edu 1235

Replace "teach.cs.toronto.edu" with the name of your machine, and "1235" with whichever port number the server is using.
