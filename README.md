This project implements a TCP-based Wordle game where the client sends 5-letter guesses to a server and receives feedback on whether the guess is valid, how many guesses remain, and which letters are correct or misplaced. 
The server loads a dictionary, chooses a random hidden word for each connection, and creates a separate thread for each player so multiple games can run at the same time. 
It also tracks global game statistics such as total wins, losses, and guesses across all sessions.
