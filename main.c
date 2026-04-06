#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int total_guesses;
int total_wins;
int total_losses;
char ** words;

/* write the wordle_server() function and place all of your code in hw3.c */
int wordle_server( int argc, char ** argv );

int main( int argc, char ** argv )
{
  total_guesses = total_wins = total_losses = 0;
  words = calloc( 1, sizeof( char * ) );
  if ( words == NULL ) { perror( "calloc() failed" ); return EXIT_FAILURE; }

  int rc = wordle_server( argc, argv );

  /* on Submitty, there will be more code here that validates
   *  the global variables at the end of your code...
   */

  /* deallocate memory for the list of words played */
  for ( char ** ptr = words ; *ptr ; ptr++ )
  {
    free( *ptr );
  }
  free( words );

  return rc;
}